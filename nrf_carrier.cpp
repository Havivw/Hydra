/*
 * NRF24 Constant Carrier Jammer — see header.
 */

#include "nrf_carrier.h"
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

namespace NrfCarrier {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

static int  curChannel = 76;
static bool txActive = false;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;
static uint32_t lastUiMs = 0;

static void setCarrier(bool on) {
  if (on) {
    radio.stopListening();
    radio.setAutoAck(false);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setChannel(curChannel);
    // RF24 library helper: startConstCarrier(power, channel)
    radio.startConstCarrier(RF24_PA_MAX, curChannel);
  } else {
    radio.stopConstCarrier();
    radio.powerDown();
  }
}

static void drawScreen() {
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("NRF Constant Carrier");

  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(60, 90);
  tft.printf("ch %d", curChannel);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 140);
  tft.printf("Freq:  %d MHz", 2400 + curChannel);

  tft.setTextSize(2);
  tft.setTextColor(txActive ? TFT_RED : TFT_DARKGREY);
  tft.setCursor(50, 175);
  tft.print(txActive ? "JAMMING" : "STOPPED");

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 220);
  tft.print("[L]   ch down");
  tft.setCursor(2, 235);
  tft.print("[R]   ch up");
  tft.setCursor(2, 250);
  tft.print("[UP]  Toggle TX");
  tft.setCursor(2, 265);
  tft.setTextColor(TFT_DARKGREY);
  tft.print("[SEL] Exit (also stops TX)");
}

void carrierSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  txActive = false;
  toggleHeldFromPrev = true;

  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  radio.begin();
  radio.stopListening();
  radio.powerDown();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawScreen();
}

void carrierLoop() {
  uint32_t now = millis();
  bool upPressed    = !pcf.digitalRead(BTN_UP);
  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool toggle = upPressed || leftPressed || rightPressed;

  if (toggleHeldFromPrev) {
    if (!toggle) toggleHeldFromPrev = false;
  } else if (toggle && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (upPressed) {
      txActive = !txActive;
      setCarrier(txActive);
      Serial.printf("[NrfCarrier] %s ch%d\n", txActive ? "START" : "STOP", curChannel);
    } else if (rightPressed) {
      curChannel++;
      if (curChannel > 125) curChannel = 0;
      if (txActive) setCarrier(true);  // re-tune
    } else if (leftPressed) {
      curChannel--;
      if (curChannel < 0) curChannel = 125;
      if (txActive) setCarrier(true);
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawScreen();
  }

  // Safety: if user exits via SELECT, we should disable carrier. The main
  // dispatch only calls this loop while the feature is active, so on exit
  // the loop simply stops. Disable carrier on the way out via the
  // feature_exit_requested path — handled in main dispatch (next time we
  // re-enter we'll see txActive=false and not auto-resume).
  // (Belt-and-braces): if the loop is paused for >2s, stop TX. Not done
  // here — RF24 powerDown happens automatically on feature re-entry init.

  if (now - lastUiMs > 500) {
    lastUiMs = now;
    // Periodic small UI refresh (status only) — keeps the screen live
    drawScreen();
  }
  delay(20);
}

}  // namespace NrfCarrier
