/*
 * Flipper Zero Detector — Hydra port
 * See flipper_detect.h for provenance.
 */

#include "flipper_detect.h"
#include "bleconfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace FlipperDetect {

// ============================================================
// ALERT QUEUE
// ============================================================
struct AlertRecord {
  uint8_t mac[6];
  char    name[24];
  int8_t  rssi;
};

#define ALERT_QUEUE_SIZE 16
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void pushAlert(const uint8_t* mac, const char* name, int8_t rssi) {
  portENTER_CRITICAL(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    int i = 0;
    while (i < 23 && name[i]) { alertQueue[alertHead].name[i] = name[i]; i++; }
    alertQueue[alertHead].name[i] = '\0';
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
    int i = 0;
    while (i < 23 && alertQueue[alertTail].name[i]) {
      out.name[i] = alertQueue[alertTail].name[i]; i++;
    }
    out.name[i] = '\0';
    out.rssi = alertQueue[alertTail].rssi;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

// ============================================================
// BLE CALLBACK
// ============================================================
class FlipperScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice dev) override {
    if (!dev.haveManufacturerData()) return;
    std::string mfg = dev.getManufacturerData();
    if (mfg.length() < 2) return;
    // Flipper manufacturer ID 0x0FBA, little-endian on the air = BA 0F
    uint8_t b0 = (uint8_t)mfg[0];
    uint8_t b1 = (uint8_t)mfg[1];
    if (!(b0 == 0xBA && b1 == 0x0F)) return;

    // Extract MAC. BLEAddress::getNative() returns `uint8_t (*)[6]` — pointer
    // to a 6-byte array — so cast to a flat byte pointer.
    BLEAddress addr = dev.getAddress();
    const uint8_t* native = (const uint8_t*)addr.getNative();
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = native[i];

    // Get name (may be empty)
    std::string nm = dev.getName();
    char nameBuf[24];
    int len = nm.length();
    if (len > 23) len = 23;
    for (int i = 0; i < len; i++) nameBuf[i] = nm[i];
    nameBuf[len] = '\0';
    if (len == 0) {
      strncpy(nameBuf, "(unnamed)", sizeof(nameBuf));
    }

    pushAlert(mac, nameBuf, (int8_t)dev.getRSSI());
  }
};

static FlipperScanCallbacks* scanCallbacks = nullptr;
static BLEScan* bleScan = nullptr;

// ============================================================
// DEDUPE  (10s per-MAC)
// ============================================================
#define DEDUPE_SLOTS 32
#define DEDUPE_COOLDOWN_MS 10000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool macEq(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (macEq(dedupeTable[i].mac, mac)) {
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

// ============================================================
// DISPLAY
// ============================================================
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
           "%02x:%02x:%02x:%02x:%02x:%02x %-12s %d",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
           r.name, (int)r.rssi);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
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
  tft.print("BLE scan");
  tft.setCursor(90, 24);
  tft.print("Hits:");
  tft.print(totalHits);
  tft.setCursor(165, 24);
  tft.print("[SEL]=exit");
}

// ============================================================
// PUBLIC API
// ============================================================
void flipperDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  totalHits = 0;
  dispRowCount = 0;
  alertHead = alertTail = 0;
  dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    memset(dedupeTable[i].mac, 0, 6);
    dedupeTable[i].ts = 0;
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Flipper Zero Detector");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Stop any prior BLE scan/advertise + WiFi promisc state from the last
  // feature, so this setup hands BLEDevice::init a clean controller.
  bleFeatureCleanup();
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  delay(50);

  // BLEDevice::init is idempotent; safe to call after cifertech features.
  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  if (scanCallbacks == nullptr) scanCallbacks = new FlipperScanCallbacks();
  bleScan->setAdvertisedDeviceCallbacks(scanCallbacks, /*wantDuplicates=*/true);
  bleScan->setActiveScan(true);
  bleScan->setInterval(100);
  bleScan->setWindow(99);
  // Start continuous scan (duration=0 means run until stopped)
  bleScan->start(0, nullptr, false);

  Serial.println("[Flipper] BLE scan started, watching for manuf-ID 0x0FBA");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  redrawTopStatus();
  lastUiUpdate = millis();
}

void flipperDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[Flipper] %02x:%02x:%02x:%02x:%02x:%02x name=%s rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  r.name, (int)r.rssi);
    redrawList();
  }

  uint32_t now = millis();
  if (now - lastUiUpdate > 250) {
    redrawTopStatus();
    lastUiUpdate = now;
  }

  delay(20);
}

}  // namespace FlipperDetect
