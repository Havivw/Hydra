/*
 * NRF24 Mousejack Scanner — see header.
 */

#include "nrf_mousejack.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>
#include <RF24.h>
#include <string.h>

#define DARK_GRAY 0x4208

// NRF1 wiring on DIV v1 (per plans/01-hardware-divv1.md)
#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

namespace NrfMousejack {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

// Mousejack-style promiscuous mode setup.
// Trick: set address width to 2, address = 0x00AA, disable CRC + auto-ack.
// The chip will then accept any packet whose preamble matches the partial
// address bits and surface the payload — including packets from devices
// that don't know we exist.
static void enablePromisc(uint8_t channel) {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);   // Logitech is 2 Mbps; MS varies
  radio.setPALevel(RF24_PA_LOW, true);
  radio.disableCRC();
  radio.setAddressWidth(2);
  uint8_t addr[2] = { 0xAA, 0x00 };
  radio.openReadingPipe(0, addr);
  radio.setChannel(channel);
  radio.setPayloadSize(32);
  radio.startListening();
}

// Channels mousejack-style sweepers typically cover. NRF24 channel = MHz - 2400.
// We hop 2..83 in steps that cover the BLE/WiFi-overlapping consumer band
// where wireless mice/keyboards live.
static const uint8_t SWEEP_CHANNELS[] = {
   2,  5,  8, 12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72, 76, 80, 83
};
static const int SWEEP_COUNT = sizeof(SWEEP_CHANNELS) / sizeof(SWEEP_CHANNELS[0]);
static int curSweep = 0;

static const uint32_t CHANNEL_DWELL_MS = 80;
static uint32_t lastHopMs = 0;
static uint32_t lastUiMs = 0;

// Detection record
struct Hit {
  uint8_t  channel;
  uint8_t  payload[6];  // first 6 bytes (typical Logitech "address" prefix)
  uint32_t lastSeen;
  uint32_t count;
};
#define MAX_HITS 12
static Hit hits[MAX_HITS];
static int hitCount = 0;
static uint32_t totalDetections = 0;

static void registerHit(uint8_t ch, const uint8_t* buf) {
  // Match by first 6 bytes
  for (int i = 0; i < hitCount; i++) {
    if (memcmp(hits[i].payload, buf, 6) == 0) {
      hits[i].channel = ch;
      hits[i].lastSeen = millis();
      hits[i].count++;
      totalDetections++;
      return;
    }
  }
  if (hitCount < MAX_HITS) {
    Hit& h = hits[hitCount++];
    h.channel = ch;
    memcpy(h.payload, buf, 6);
    h.lastSeen = millis();
    h.count = 1;
    totalDetections++;
  }
}

// Logitech Unifying / Microsoft devices use 5-byte addresses; the first
// 2-3 bytes of payload after the address are usually consistent device
// state. We do a coarse plausibility filter: reject all-zero, all-0xFF,
// or known-bogus preambles before reporting.
static bool plausiblePacket(const uint8_t* p) {
  bool allZero = true;
  bool allFF   = true;
  for (int i = 0; i < 6; i++) {
    if (p[i] != 0x00) allZero = false;
    if (p[i] != 0xFF) allFF = false;
  }
  if (allZero || allFF) return false;
  return true;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Mousejack  ch%d  hits:%u  [SEL]=exit",
             (int)SWEEP_CHANNELS[curSweep], (unsigned)totalDetections);
}

static void drawHits() {
  tft.fillRect(0, 65, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(2, 70);
  tft.print("ch  AA:BB:CC:DD:EE:FF  count age");

  if (hitCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 90);
    tft.print("(no devices detected)");
    return;
  }
  uint32_t now = millis();
  for (int i = 0; i < hitCount; i++) {
    int y = 90 + i * 14;
    uint32_t ageMs = now - hits[i].lastSeen;
    tft.setTextColor(ageMs < 2000 ? TFT_GREEN : (ageMs < 8000 ? TFT_YELLOW : TFT_DARKGREY),
                     TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf("%2d  %02x:%02x:%02x:%02x:%02x:%02x  %4u  %us",
               (int)hits[i].channel,
               hits[i].payload[0], hits[i].payload[1], hits[i].payload[2],
               hits[i].payload[3], hits[i].payload[4], hits[i].payload[5],
               (unsigned)hits[i].count,
               (unsigned)(ageMs / 1000));
  }
}

void mousejackSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  hitCount = 0;
  totalDetections = 0;
  curSweep = 0;

  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  if (!radio.begin()) {
    Serial.println("[Mousejack] radio.begin failed!");
  }
  enablePromisc(SWEEP_CHANNELS[curSweep]);

  Serial.println("[Mousejack] promiscuous mode armed, sweeping consumer-band channels");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawHits();
  lastHopMs = millis();
  lastUiMs = millis();
}

void mousejackLoop() {
  uint32_t now = millis();
  if (now - lastHopMs >= CHANNEL_DWELL_MS) {
    curSweep = (curSweep + 1) % SWEEP_COUNT;
    radio.setChannel(SWEEP_CHANNELS[curSweep]);
    lastHopMs = now;
  }

  while (radio.available()) {
    uint8_t buf[32];
    radio.read(buf, 32);
    if (plausiblePacket(buf)) {
      registerHit(SWEEP_CHANNELS[curSweep], buf);
    }
  }

  if (now - lastUiMs > 250) {
    drawHeader();
    drawHits();
    lastUiMs = now;
  }
  delay(5);
}

}  // namespace NrfMousejack
