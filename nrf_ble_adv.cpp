/*
 * BLE Advertising Monitor — see header.
 */

#include "nrf_ble_adv.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>
#include <RF24.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

#define BTN_UP    6
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 5

namespace NrfBleAdv {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

// BLE advertising channels and their NRF24 equivalents.
struct BleAdvChan { uint8_t bleNum; uint8_t nrfCh; uint16_t freqMhz; };
static const BleAdvChan ADV_CHANS[] = {
  {37, 2,  2402},
  {38, 26, 2426},
  {39, 80, 2480}
};
static const int ADV_CHAN_COUNT = 3;
static int curSlot = 0;
static bool autoHop = true;
static uint32_t lastHopMs = 0;
static const uint32_t HOP_DWELL_MS = 800;
static uint32_t lastUiMs = 0;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool toggleHeldFromPrev = true;

// Detections — keyed by first 6 bytes (likely device address bytes)
struct Hit {
  uint8_t  bytes[6];
  uint8_t  bleChan;
  uint32_t count;
  uint32_t lastSeen;
};
#define MAX_HITS 14
static Hit hits[MAX_HITS];
static int hitCount = 0;
static uint32_t totalCaptured = 0;

// BLE access address 0x8E89BED6 → reversed bit order each byte for NRF24
// (NRF24 sends MSB first; BLE is LSB first). Reversal table:
//   0x8E -> 0x71, 0x89 -> 0x91, 0xBE -> 0x7D, 0xD6 -> 0x6B
// Result: {0x6B, 0x7D, 0x91, 0x71} — the bit-reversed access address.
static uint8_t bleAddrReversed[4] = { 0x6B, 0x7D, 0x91, 0x71 };

static void enableBleRx(uint8_t nrfCh) {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_1MBPS);   // BLE is 1 Mbps
  radio.setPALevel(RF24_PA_LOW, true);
  radio.disableCRC();
  radio.setAddressWidth(4);
  radio.openReadingPipe(0, bleAddrReversed);
  radio.setChannel(nrfCh);
  radio.setPayloadSize(32);
  radio.startListening();
}

static bool plausible(const uint8_t* p) {
  bool allZero = true, allFF = true;
  for (int i = 0; i < 6; i++) {
    if (p[i] != 0x00) allZero = false;
    if (p[i] != 0xFF) allFF = false;
  }
  return !(allZero || allFF);
}

static void registerHit(uint8_t bleChan, const uint8_t* buf) {
  for (int i = 0; i < hitCount; i++) {
    if (memcmp(hits[i].bytes, buf, 6) == 0) {
      hits[i].count++;
      hits[i].bleChan = bleChan;
      hits[i].lastSeen = millis();
      totalCaptured++;
      return;
    }
  }
  if (hitCount < MAX_HITS) {
    Hit& h = hits[hitCount++];
    memcpy(h.bytes, buf, 6);
    h.bleChan = bleChan;
    h.count = 1;
    h.lastSeen = millis();
    totalCaptured++;
  }
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("BLE Adv  ch%d(%uMHz) %s  %u",
             ADV_CHANS[curSlot].bleNum,
             ADV_CHANS[curSlot].freqMhz,
             autoHop ? "HOP" : "STAY",
             (unsigned)totalCaptured);
}

static void drawHits() {
  tft.fillRect(0, 65, tft.width(), tft.height() - 75, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 70);
  tft.print("ble  payload(first6)         cnt age");

  uint32_t now = millis();
  for (int i = 0; i < hitCount; i++) {
    int y = 88 + i * 14;
    uint32_t age = now - hits[i].lastSeen;
    tft.setTextColor(age < 2000 ? TFT_GREEN : (age < 10000 ? TFT_YELLOW : TFT_DARKGREY));
    tft.setCursor(2, y);
    tft.printf("%d  %02x%02x%02x%02x%02x%02x   %4u %us",
               (int)hits[i].bleChan,
               hits[i].bytes[0], hits[i].bytes[1], hits[i].bytes[2],
               hits[i].bytes[3], hits[i].bytes[4], hits[i].bytes[5],
               (unsigned)hits[i].count,
               (unsigned)(age / 1000));
  }
  if (hitCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 90);
    tft.print("(no BLE adv detected — try near a phone)");
  }
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, tft.height() - 16);
  tft.print("[U/D]ch  [L/R]hop  [SEL]exit");
}

void bleAdvSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  curSlot = 0; autoHop = true; hitCount = 0; totalCaptured = 0;
  toggleHeldFromPrev = true;

  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  radio.begin();
  enableBleRx(ADV_CHANS[curSlot].nrfCh);
  Serial.println("[BleAdv] armed on adv channel 37");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawHits();
  lastHopMs = millis();
  lastUiMs = millis();
}

void bleAdvLoop() {
  uint32_t now = millis();
  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (left || right) {
      autoHop = !autoHop;
    } else if (down) {
      curSlot = (curSlot + 1) % ADV_CHAN_COUNT;
      enableBleRx(ADV_CHANS[curSlot].nrfCh);
    } else if (up) {
      curSlot = (curSlot - 1 + ADV_CHAN_COUNT) % ADV_CHAN_COUNT;
      enableBleRx(ADV_CHANS[curSlot].nrfCh);
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawHeader();
  }

  // Auto-hop across 37/38/39
  if (autoHop && now - lastHopMs >= HOP_DWELL_MS) {
    curSlot = (curSlot + 1) % ADV_CHAN_COUNT;
    enableBleRx(ADV_CHANS[curSlot].nrfCh);
    lastHopMs = now;
  }

  // Drain
  bool gotSome = false;
  while (radio.available()) {
    uint8_t buf[32];
    radio.read(buf, 32);
    if (plausible(buf)) {
      registerHit(ADV_CHANS[curSlot].bleNum, buf);
      gotSome = true;
    }
  }

  if (gotSome || now - lastUiMs > 300) {
    drawHeader();
    drawHits();
    lastUiMs = now;
  }
  delay(3);
}

}  // namespace NrfBleAdv
