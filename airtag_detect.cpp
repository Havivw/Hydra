/*
 * AirTag Detector — Hydra port. See header for provenance.
 */

#include "airtag_detect.h"
#include "bleconfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace AirTagDetect {

struct AlertRecord { uint8_t mac[6]; int8_t rssi; };

#define ALERT_QUEUE_SIZE 16
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void pushAlert(const uint8_t* mac, int8_t rssi) {
  portENTER_CRITICAL(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    alertQueue[alertHead].rssi = rssi;
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
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

class AirTagScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice dev) override {
    uint8_t* payload = dev.getPayload();
    size_t len = dev.getPayloadLength();
    if (!payload || len < 4) return;

    bool match = false;
    for (size_t i = 0; i + 3 < len; i++) {
      if ((payload[i] == 0x1E && payload[i+1] == 0xFF && payload[i+2] == 0x4C && payload[i+3] == 0x00) ||
          (payload[i] == 0x4C && payload[i+1] == 0x00 && payload[i+2] == 0x12 && payload[i+3] == 0x19)) {
        match = true; break;
      }
    }
    if (!match) return;

    BLEAddress addr = dev.getAddress();
    const uint8_t* native = (const uint8_t*)addr.getNative();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = native[i];
    pushAlert(mac, (int8_t)dev.getRSSI());
  }
};

static AirTagScanCallbacks* scanCallbacks = nullptr;
static BLEScan* bleScan = nullptr;

#define DEDUPE_SLOTS 32
#define DEDUPE_COOLDOWN_MS 30000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (memcmp(dedupeTable[i].mac, mac, 6) == 0) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now;
      return false;
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
struct DispRow { char text[40]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static void pushDispRow(const AlertRecord& r) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x:%02x:%02x:%02x  rssi=%d",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5], (int)r.rssi);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.print("BLE/AirTag");
  tft.setCursor(110, 24);
  tft.print("Hits:");
  tft.print(totalHits);
  tft.setCursor(165, 24);
  tft.print("[SEL]=exit");
}

void airtagDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  totalHits = 0; dispRowCount = 0;
  alertHead = alertTail = 0; dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) { memset(dedupeTable[i].mac, 0, 6); dedupeTable[i].ts = 0; }

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] AirTag (Find My) Detector");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Stop any prior BLE scan/advertise + WiFi promisc state from the last
  // feature, so this setup hands BLEDevice::init a clean controller.
  bleFeatureCleanup();
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  delay(50);

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  if (scanCallbacks == nullptr) scanCallbacks = new AirTagScanCallbacks();
  // wantDuplicates=true (the default). Counterintuitive but necessary:
  // with duplicates=false, the BLE library stores every unique-MAC
  // BLEAdvertisedDevice in m_scanResults and never frees them during the
  // scan (see BLEScan.cpp:130 in core 2.0.17). In a crowd the lib leaks
  // ~200B per unique BLE device the radio sees (not just Apple matches)
  // and the heap fragments fast — observed crash after ~2 visible alerts
  // because the radio had seen 50+ devices behind the scenes.
  // Our own dedupeTable still gates user-visible alerts to one per MAC.
  bleScan->setAdvertisedDeviceCallbacks(scanCallbacks, true);
  bleScan->setActiveScan(true);
  // 40% duty cycle (was 99%). The detector doesn't need to be on-air all
  // the time; lower duty cycle lets the BT host task breathe.
  bleScan->setInterval(200); bleScan->setWindow(80);
  bleScan->start(0, nullptr, false);
  Serial.println("[AirTag] BLE scan started");

  float v = readBatteryVoltage(); drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  redrawTopStatus();
  lastUiUpdate = millis();
}

void airtagDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[AirTag] %02x:%02x:%02x:%02x:%02x:%02x rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5], (int)r.rssi);
    redrawList();
  }
  uint32_t now = millis();
  if (now - lastUiUpdate > 250) { redrawTopStatus(); lastUiUpdate = now; }
  delay(20);
}

}  // namespace AirTagDetect
