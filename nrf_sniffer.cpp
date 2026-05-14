/*
 * NRF24 Promiscuous Sniffer — see header.
 */

#include "nrf_sniffer.h"
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

namespace NrfSniffer {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

static int  curChannel = 76;     // start on a common consumer channel
static bool autoHop = true;
static const uint32_t HOP_DWELL_MS = 200;
static uint32_t lastHopMs = 0;
static uint32_t lastUiMs = 0;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;

// Last N captured packets (just first 8 bytes of payload — enough to ID
// most short proprietary protocols)
#define LOG_ROWS 14
#define ROW_HEIGHT 14
#define LOG_TOP_Y 65
struct PktRow {
  uint8_t ch;
  uint8_t buf[8];
};
static PktRow log[LOG_ROWS];
static int logCount = 0;
static uint32_t totalCaptured = 0;

static void enablePromisc(uint8_t channel) {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_LOW, true);
  radio.disableCRC();
  radio.setAddressWidth(2);
  uint8_t addr[2] = { 0xAA, 0x00 };
  radio.openReadingPipe(0, addr);
  radio.setChannel(channel);
  radio.setPayloadSize(32);
  radio.startListening();
}

static bool plausible(const uint8_t* p) {
  bool allZero = true, allFF = true;
  for (int i = 0; i < 8; i++) {
    if (p[i] != 0x00) allZero = false;
    if (p[i] != 0xFF) allFF = false;
  }
  return !(allZero || allFF);
}

static void pushPkt(uint8_t ch, const uint8_t* buf) {
  if (logCount == LOG_ROWS) {
    for (int i = 1; i < LOG_ROWS; i++) log[i - 1] = log[i];
    logCount--;
  }
  PktRow& r = log[logCount++];
  r.ch = ch;
  memcpy(r.buf, buf, 8);
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Sniffer  ch%d  %s  total:%u",
             curChannel,
             autoHop ? "HOP" : "STAY",
             (unsigned)totalCaptured);
}

static void drawLog() {
  tft.fillRect(0, LOG_TOP_Y, tft.width(), LOG_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  for (int i = 0; i < logCount; i++) {
    tft.setCursor(2, LOG_TOP_Y + i * ROW_HEIGHT);
    tft.printf("ch%2d %02x%02x %02x%02x %02x%02x %02x%02x",
               (int)log[i].ch,
               log[i].buf[0], log[i].buf[1], log[i].buf[2], log[i].buf[3],
               log[i].buf[4], log[i].buf[5], log[i].buf[6], log[i].buf[7]);
  }
  if (logCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, LOG_TOP_Y);
    tft.print("(no packets captured yet)");
  }
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, tft.height() - 16);
  tft.print("[L/R]ch  [UP]hop  [SEL]exit");
}

void sniffSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  logCount = 0;
  totalCaptured = 0;
  curChannel = 76;
  autoHop = true;
  toggleHeldFromPrev = true;

  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  radio.begin();
  enablePromisc(curChannel);
  Serial.println("[NrfSniff] promiscuous sniffer armed");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawLog();
  lastHopMs = millis();
  lastUiMs = millis();
}

void sniffLoop() {
  uint32_t now = millis();

  // Manual L/R channel change (wait-for-release)
  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool upPressed    = !pcf.digitalRead(BTN_UP);
  bool toggle = leftPressed || rightPressed || upPressed;

  if (toggleHeldFromPrev) {
    if (!toggle) toggleHeldFromPrev = false;
  } else if (toggle && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (upPressed) {
      autoHop = !autoHop;
    } else if (rightPressed) {
      curChannel = (curChannel + 1) % 126;
      if (curChannel < 2) curChannel = 2;
      radio.setChannel(curChannel);
    } else if (leftPressed) {
      curChannel--;
      if (curChannel < 2) curChannel = 125;
      radio.setChannel(curChannel);
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawHeader();
  }

  // Auto-hop
  if (autoHop && now - lastHopMs >= HOP_DWELL_MS) {
    curChannel++;
    if (curChannel > 125) curChannel = 2;
    radio.setChannel(curChannel);
    lastHopMs = now;
  }

  // Drain captured packets
  bool gotSome = false;
  while (radio.available()) {
    uint8_t buf[32];
    radio.read(buf, 32);
    if (plausible(buf)) {
      pushPkt(curChannel, buf);
      totalCaptured++;
      gotSome = true;
    }
  }

  if (gotSome || now - lastUiMs > 200) {
    drawHeader();
    drawLog();
    lastUiMs = now;
  }

  delay(2);
}

}  // namespace NrfSniffer
