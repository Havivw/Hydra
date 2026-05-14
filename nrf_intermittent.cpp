/*
 * NRF24 Intermittent Jammer — see header.
 */

#include "nrf_intermittent.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>
#include <RF24.h>

#define DARK_GRAY 0x4208

#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

#define BTN_UP    6
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 5

namespace NrfIntermittent {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

static int  curChannel = 76;
static bool active = false;
static int  onMs  = 30;   // burst length
static int  offMs = 10;   // pause length
static uint32_t lastSwitchMs = 0;
static bool carrierOn = false;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;
static uint32_t lastUiMs = 0;
static uint32_t totalBursts = 0;

static void carrierStart() {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setChannel(curChannel);
  radio.startConstCarrier(RF24_PA_MAX, curChannel);
}

static void carrierStop() {
  radio.stopConstCarrier();
  radio.powerDown();
}

static void drawScreen() {
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("NRF Intermittent Jammer");

  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(60, 75);
  tft.printf("ch %d", curChannel);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 120);
  tft.printf("Freq:  %d MHz", 2400 + curChannel);
  tft.setCursor(2, 135);
  tft.printf("On %d ms / Off %d ms", onMs, offMs);
  tft.setCursor(2, 150);
  int duty = (onMs + offMs > 0) ? (100 * onMs) / (onMs + offMs) : 0;
  tft.printf("Duty:  %d%%", duty);

  tft.setTextSize(2);
  tft.setTextColor(active ? TFT_RED : TFT_DARKGREY);
  tft.setCursor(50, 180);
  tft.print(active ? "JAMMING" : "STOPPED");

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 215);
  tft.printf("Bursts: %u", (unsigned)totalBursts);

  tft.setCursor(2, 235);
  tft.print("[UP]    Toggle TX");
  tft.setCursor(2, 250);
  tft.print("[L/R]   Channel");
  tft.setCursor(2, 265);
  tft.print("[DOWN]  Cycle duty (10/30/50/70%)");
  tft.setCursor(2, 280);
  tft.setTextColor(TFT_DARKGREY);
  tft.print("[SEL]   Exit (stops TX)");
}

void intermittentSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  active = false;
  carrierOn = false;
  totalBursts = 0;
  toggleHeldFromPrev = true;

  SPI.begin(18, 19, 23, 17);
  radio.begin();
  radio.stopListening();
  radio.powerDown();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawScreen();
  lastSwitchMs = millis();
  lastUiMs = millis();
}

static void cycleDuty() {
  // 10/30/50/70% with total period 40ms
  int duty = (onMs * 100) / (onMs + offMs);
  if (duty < 20)      { onMs = 12; offMs = 28; }   // 30%
  else if (duty < 40) { onMs = 20; offMs = 20; }   // 50%
  else if (duty < 60) { onMs = 28; offMs = 12; }   // 70%
  else                { onMs = 4;  offMs = 36; }   // 10%
}

void intermittentLoop() {
  uint32_t now = millis();

  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (up) {
      active = !active;
      if (!active) {
        if (carrierOn) { carrierStop(); carrierOn = false; }
      }
      Serial.printf("[NrfIntermittent] %s\n", active ? "STARTED" : "STOPPED");
    } else if (right) {
      curChannel = (curChannel + 1) % 126;
      if (active && carrierOn) { carrierStop(); carrierStart(); }
    } else if (left) {
      curChannel = (curChannel == 0) ? 125 : curChannel - 1;
      if (active && carrierOn) { carrierStop(); carrierStart(); }
    } else if (down) {
      cycleDuty();
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawScreen();
  }

  if (active) {
    if (carrierOn && now - lastSwitchMs >= (uint32_t)onMs) {
      carrierStop(); carrierOn = false;
      lastSwitchMs = now;
    } else if (!carrierOn && now - lastSwitchMs >= (uint32_t)offMs) {
      carrierStart(); carrierOn = true;
      lastSwitchMs = now;
      totalBursts++;
    }
  } else {
    delay(20);
  }

  if (now - lastUiMs > 500) {
    drawScreen();
    lastUiMs = now;
  }
}

}  // namespace NrfIntermittent
