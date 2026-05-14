/*
 * Ray-Ban Meta Detector — Hydra port. See header.
 */

#include "rayban_detect.h"
#include "bleconfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEUUID.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace RaybanDetect {

static const uint16_t META_IDS[] = {0xFD5F, 0xFEB7, 0xFEB8, 0x01AB, 0x058E, 0x0D53};
static const int META_ID_COUNT = sizeof(META_IDS) / sizeof(META_IDS[0]);
static const uint16_t BLOCKED_IDS[] = {0xFD5A, 0xFD69, 0x004C, 0x0006, 0xFEF3};
static const int BLOCKED_ID_COUNT = sizeof(BLOCKED_IDS) / sizeof(BLOCKED_IDS[0]);

static bool isMeta(uint16_t id) {
  for (int i = 0; i < META_ID_COUNT; i++) if (META_IDS[i] == id) return true;
  return false;
}
static bool isBlocked(uint16_t id) {
  for (int i = 0; i < BLOCKED_ID_COUNT; i++) if (BLOCKED_IDS[i] == id) return true;
  return false;
}

// Extract the 16-bit-shortened form directly from BLEUUID's native esp_bt_uuid_t.
// The previous implementation went BLEUUID->toString()->String->toLowerCase->strtol
// which allocated ~100B per call. In a dense RF environment the callback fired
// constantly and the heap fragmented enough to hang the BT host task.
static uint16_t shortIdFromBLEUUID(BLEUUID& u) {
  esp_bt_uuid_t* native = u.getNative();
  if (!native) return 0;
  if (native->len == ESP_UUID_LEN_16) {
    return native->uuid.uuid16;
  }
  if (native->len == ESP_UUID_LEN_32) {
    return (uint16_t)native->uuid.uuid32;
  }
  if (native->len == ESP_UUID_LEN_128) {
    // BT base UUID: 0000xxxx-0000-1000-8000-00805F9B34FB. ESP-IDF stores the
    // 128-bit form little-endian, so the 16-bit short id sits at bytes 12-13.
    return ((uint16_t)native->uuid.uuid128[13] << 8) | native->uuid.uuid128[12];
  }
  return 0;
}

struct AlertRecord { uint8_t mac[6]; int8_t rssi; uint16_t id; };

#define ALERT_QUEUE_SIZE 16
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void pushAlert(const uint8_t* mac, int8_t rssi, uint16_t id) {
  portENTER_CRITICAL(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    alertQueue[alertHead].rssi = rssi;
    alertQueue[alertHead].id = id;
    alertHead = nextHead;
  }
  portEXIT_CRITICAL(&alertMux);
}

static bool popAlert(AlertRecord& out) {
  bool got = false;
  portENTER_CRITICAL(&alertMux);
  if (alertTail != alertHead) {
    for (int i = 0; i < 6; i++) out.mac[i] = alertQueue[alertTail].mac[i];
    out.rssi = alertQueue[alertTail].rssi;
    out.id = alertQueue[alertTail].id;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

class RaybanScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice dev) override {
    bool match = false;
    uint16_t matched_id = 0;

    // Manufacturer data — first 2 bytes are the company ID (little-endian)
    if (dev.haveManufacturerData()) {
      std::string m = dev.getManufacturerData();
      if (m.length() >= 2) {
        uint16_t cid = ((uint8_t)m[1] << 8) | (uint8_t)m[0];
        if (isBlocked(cid)) return;
        if (isMeta(cid)) { match = true; matched_id = cid; }
      }
    }
    // Service UUID(s) — allocation-free path via native esp_bt_uuid_t.
    if (!match && dev.haveServiceUUID()) {
      int n = dev.getServiceUUIDCount();
      for (int i = 0; i < n; i++) {
        BLEUUID u = dev.getServiceUUID(i);
        uint16_t sid = shortIdFromBLEUUID(u);
        if (sid == 0) continue;
        if (isBlocked(sid)) return;
        if (isMeta(sid)) { match = true; matched_id = sid; break; }
      }
    }
    // Service Data — same allocation-free path.
    if (!match && dev.haveServiceData()) {
      BLEUUID u = dev.getServiceDataUUID();
      uint16_t sid = shortIdFromBLEUUID(u);
      if (sid != 0) {
        if (isBlocked(sid)) return;
        if (isMeta(sid)) { match = true; matched_id = sid; }
      }
    }
    if (!match) return;

    BLEAddress addr = dev.getAddress();
    const uint8_t* native = (const uint8_t*)addr.getNative();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = native[i];
    pushAlert(mac, (int8_t)dev.getRSSI(), matched_id);
  }
};

static RaybanScanCallbacks* scanCallbacks = nullptr;
static BLEScan* bleScan = nullptr;

#define DEDUPE_SLOTS 16
#define DEDUPE_COOLDOWN_MS 30000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (memcmp(dedupeTable[i].mac, mac, 6) == 0) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now; return false;
    }
  }
  memcpy(dedupeTable[dedupeIdx].mac, mac, 6);
  dedupeTable[dedupeIdx].ts = now;
  dedupeIdx = (dedupeIdx + 1) % DEDUPE_SLOTS;
  return false;
}

static uint32_t totalHits = 0;
static uint32_t lastUiUpdate = 0;
#define LIST_ROWS 14
#define ROW_HEIGHT 14
#define LIST_TOP_Y 60
struct DispRow { char text[44]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static void pushDispRow(const AlertRecord& r) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x:%02x:%02x:%02x id=%04x %d",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
           (unsigned)r.id, (int)r.rssi);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.print("Meta/Ray-Ban");
  tft.setCursor(115, 24);
  tft.print("Hits:");
  tft.print(totalHits);
  tft.setCursor(165, 24);
  tft.print("[SEL]=exit");
}

void raybanDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  totalHits = 0; dispRowCount = 0;
  alertHead = alertTail = 0; dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) { memset(dedupeTable[i].mac, 0, 6); dedupeTable[i].ts = 0; }

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Ray-Ban/Meta Glasses Det.");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Stop any prior BLE scan/advertise + WiFi promisc state from the last
  // feature, so this setup hands BLEDevice::init a clean controller.
  bleFeatureCleanup();
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  delay(50);

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  if (scanCallbacks == nullptr) scanCallbacks = new RaybanScanCallbacks();
  // wantDuplicates=true (the default). Counterintuitive but necessary:
  // with duplicates=false, BLEScan.cpp:130 in core 2.0.17 leaks a
  // BLEAdvertisedDevice for every unique MAC seen (not just Meta matches).
  // In a crowd this fragments the heap and crashes the firmware. Our own
  // dedupeTable gates user-visible alerts to one per MAC.
  // The heavy heap-allocating UUID parsing in onResult has been replaced
  // with a getNative()-based path, so duplicate callbacks are now cheap.
  bleScan->setAdvertisedDeviceCallbacks(scanCallbacks, true);
  bleScan->setActiveScan(true);
  // 40% duty cycle (was 99%) — gives the BT host task slack to drain events.
  bleScan->setInterval(200); bleScan->setWindow(80);
  bleScan->start(0, nullptr, false);
  Serial.println("[Rayban] BLE scan started");

  float v = readBatteryVoltage(); drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  redrawTopStatus();
  lastUiUpdate = millis();
}

void raybanDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[Rayban] %02x:%02x:%02x:%02x:%02x:%02x id=%04x rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  (unsigned)r.id, (int)r.rssi);
    redrawList();
  }
  uint32_t now = millis();
  if (now - lastUiUpdate > 250) { redrawTopStatus(); lastUiUpdate = now; }
  delay(20);
}

}  // namespace RaybanDetect
