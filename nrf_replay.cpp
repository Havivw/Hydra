/*
 * NRF24 Capture + Replay — see header.
 */

#include "nrf_replay.h"
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

namespace NrfReplay {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

enum Phase { PHASE_IDLE = 0, PHASE_CAPTURING = 1, PHASE_LOADED = 2, PHASE_REPLAYING = 3 };
static Phase phase = PHASE_IDLE;

static int  curChannel = 76;
static uint8_t captured[32];
static bool    haveCapture = false;
static uint32_t capturedAtMs = 0;
static uint32_t replayCount = 0;
static uint32_t replayStartedAt = 0;

static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;

static void armRx(uint8_t channel) {
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

static void armTx(uint8_t channel) {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_HIGH, true);
  radio.disableCRC();
  radio.setAddressWidth(2);
  uint8_t addr[2] = { 0xAA, 0x00 };
  radio.openWritingPipe(addr);
  radio.setChannel(channel);
  radio.setPayloadSize(32);
}

static bool plausible(const uint8_t* p) {
  bool allZero = true, allFF = true;
  for (int i = 0; i < 8; i++) {
    if (p[i] != 0x00) allZero = false;
    if (p[i] != 0xFF) allFF = false;
  }
  return !(allZero || allFF);
}

static void drawScreen() {
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("NRF24 Capture + Replay");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 70);
  tft.printf("Channel: %d  (%u MHz)", curChannel, (unsigned)(2400 + curChannel));

  // State badge
  const char* badge =
    phase == PHASE_CAPTURING ? "CAPTURING..."
    : phase == PHASE_REPLAYING ? "REPLAYING"
    : haveCapture            ? "LOADED"
                              : "EMPTY";
  uint16_t badgeColor =
    phase == PHASE_CAPTURING ? TFT_YELLOW
    : phase == PHASE_REPLAYING ? TFT_RED
    : haveCapture            ? TFT_GREEN
                              : TFT_DARKGREY;
  tft.setTextSize(2);
  tft.setTextColor(badgeColor);
  tft.setCursor(60, 95);
  tft.print(badge);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  if (haveCapture) {
    tft.setCursor(2, 130);
    tft.print("Captured payload (first 16B):");
    tft.setCursor(2, 145);
    for (int i = 0; i < 16; i++) tft.printf("%02x ", captured[i]);
  } else {
    tft.setCursor(2, 130);
    tft.setTextColor(TFT_DARKGREY);
    tft.print("(no packet captured yet)");
  }

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 195);
  tft.printf("Replay count: %u", (unsigned)replayCount);

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 230);
  tft.print("[UP]   capture / replay");
  tft.setCursor(2, 245);
  tft.print("[DOWN] clear capture");
  tft.setCursor(2, 260);
  tft.print("[L/R]  channel");
  tft.setCursor(2, 275);
  tft.print("[SEL]  exit");
}

void replaySetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  phase = PHASE_IDLE;
  haveCapture = false;
  memset(captured, 0, sizeof(captured));
  replayCount = 0;
  curChannel = 76;
  toggleHeldFromPrev = true;

  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  radio.begin();
  radio.stopListening();
  radio.powerDown();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawScreen();
}

void replayLoop() {
  uint32_t now = millis();

  // If currently CAPTURING, drain radio looking for a packet
  if (phase == PHASE_CAPTURING) {
    if (radio.available()) {
      uint8_t buf[32];
      radio.read(buf, 32);
      if (plausible(buf)) {
        memcpy(captured, buf, 32);
        haveCapture = true;
        capturedAtMs = now;
        phase = PHASE_LOADED;
        Serial.printf("[NrfReplay] captured one packet on ch%d\n", curChannel);
        drawScreen();
      }
    }
    // Allow channel changes / cancel via DOWN while capturing
    if (!pcf.digitalRead(BTN_DOWN)) {
      phase = PHASE_IDLE;
      drawScreen();
    }
  }

  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (up) {
      if (!haveCapture) {
        phase = PHASE_CAPTURING;
        armRx(curChannel);
        Serial.printf("[NrfReplay] capturing on ch%d...\n", curChannel);
      } else {
        // Replay burst — 8 TX in quick succession
        phase = PHASE_REPLAYING;
        replayStartedAt = now;
        drawScreen();
        armTx(curChannel);
        for (int i = 0; i < 8; i++) {
          radio.writeFast(captured, 32);
          delay(2);
        }
        replayCount += 8;
        phase = PHASE_LOADED;
        Serial.printf("[NrfReplay] replayed 8 frames on ch%d\n", curChannel);
      }
    } else if (down) {
      haveCapture = false;
      phase = PHASE_IDLE;
      memset(captured, 0, sizeof(captured));
      Serial.println("[NrfReplay] capture cleared");
    } else if (right) {
      curChannel++;
      if (curChannel > 125) curChannel = 0;
      if (phase == PHASE_CAPTURING) armRx(curChannel);
    } else if (left) {
      curChannel--;
      if (curChannel < 0) curChannel = 125;
      if (phase == PHASE_CAPTURING) armRx(curChannel);
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawScreen();
  }

  delay(5);
}

}  // namespace NrfReplay
