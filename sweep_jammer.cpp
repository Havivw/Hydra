/*
 * CC1101 Frequency Sweep Jammer — see header.
 */

#include "sweep_jammer.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include "subconfig.h"  // brings in ELECHOUSE_CC1101_ESP32DIV + CC1101_TXFIFO/STX
#include "shared.h"
#include "sub_shared.h"

#define TX_PIN CC1101_GDO0_PIN

namespace SweepJammer {

static bool active = false;
static bool selectedOnly = true;
static uint16_t activeIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int activeCount = 0;
static int cursor = 0;
static uint32_t framesSent = 0;
static uint32_t lastUiMs = 0;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool toggleHeldFromPrev = true;

static void rebuildActive() {
  activeCount = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (!selectedOnly || WardriveConfig::channels[i].selected) {
      activeIdx[activeCount++] = i;
    }
  }
  cursor = 0;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(active ? TFT_RED : TFT_WHITE);
  tft.setCursor(2, 45);
  tft.printf("Sweep Jammer  %s  ch%d/%d",
             active ? "ON" : "OFF",
             cursor + 1, activeCount);
}

static void drawBody() {
  tft.fillRect(0, 65, tft.width(), tft.height() - 80, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 70);
  tft.print("CC1101 fast frequency sweep");

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 85);
  tft.print("3 ms dwell per channel");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 110);
  tft.printf("Mode: %s",
             selectedOnly ? "Selected channels" : "All 30 channels");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 125);
  tft.printf("Active count: %d", activeCount);
  tft.setCursor(2, 140);
  tft.printf("Bursts sent:  %u", (unsigned)framesSent);

  if (activeCount > 0) {
    uint32_t hz = WardriveConfig::channels[activeIdx[cursor]].hz;
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(2, 160);
    tft.printf("Now: %.3f MHz", hz / 1000000.0f);
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(2, 160);
    tft.print("No channels selected!");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 175);
    tft.print("Press LEFT to switch to ALL");
  }

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 215);
  tft.print("[UP]    Toggle jam ON/OFF");
  tft.setCursor(2, 230);
  tft.print("[LEFT]  Selected <-> All");
  tft.setCursor(2, 245);
  tft.print("[SEL]   Exit (stops TX)");
}

void sweepJammerSetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  WardriveConfig::ensureInit();
  active = false;
  framesSent = 0;
  toggleHeldFromPrev = true;
  rebuildActive();

  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(0);
  ELECHOUSE_cc1101.setRxBW(500.0);
  ELECHOUSE_cc1101.setPA(12);
  if (activeCount > 0) {
    ELECHOUSE_cc1101.setMHZ(WardriveConfig::channels[activeIdx[0]].hz / 1000000.0f);
  }

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawBody();
  lastUiMs = millis();
}

void sweepJammerLoop() {
  uint32_t now = millis();

  bool up    = !pcf.digitalRead(BTN_UP);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool any = up || left;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (up) {
      active = !active;
      if (!active) {
        ELECHOUSE_cc1101.setSidle();
        digitalWrite(TX_PIN, LOW);
      }
      Serial.printf("[SweepJam] %s\n", active ? "STARTED" : "STOPPED");
    } else if (left) {
      // Stop TX before reshuffling channel list
      bool wasActive = active;
      active = false;
      ELECHOUSE_cc1101.setSidle();
      digitalWrite(TX_PIN, LOW);
      selectedOnly = !selectedOnly;
      rebuildActive();
      active = wasActive && activeCount > 0;
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawHeader();
    drawBody();
  }

  if (active && activeCount > 0) {
    // Walk to the next channel, fire a brief carrier burst, advance
    cursor = (cursor + 1) % activeCount;
    uint32_t hz = WardriveConfig::channels[activeIdx[cursor]].hz;
    ELECHOUSE_cc1101.setMHZ(hz / 1000000.0f);
    ELECHOUSE_cc1101.SetTx();

    // Constant-carrier blast for ~3ms
    ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, 0xFF);
    ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
    digitalWrite(TX_PIN, HIGH);
    delayMicroseconds(3000);
    framesSent++;
  } else {
    delay(20);
  }

  if (now - lastUiMs > 250) {
    drawHeader();
    if (active && activeCount > 0) {
      // Just refresh the "Now:" line + bursts counter (avoid full body redraw flicker)
      tft.fillRect(0, 140, tft.width(), 30, TFT_BLACK);
      tft.setTextFont(1); tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(2, 140);
      tft.printf("Bursts sent:  %u", (unsigned)framesSent);
      uint32_t hz = WardriveConfig::channels[activeIdx[cursor]].hz;
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(2, 160);
      tft.printf("Now: %.3f MHz", hz / 1000000.0f);
    }
    lastUiMs = now;
  }
}

}  // namespace SweepJammer
