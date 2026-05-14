/*
 * NRF Mode Jammer — see header.
 */

#include "nrf_mode_jammer.h"
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

namespace NrfModeJammer {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);

enum Profile { P_BLE = 0, P_BT = 1, P_WIFI = 2, P_FULL = 3, P_COUNT = 4 };

// BLE advertising channels in NRF24 channel numbering:
//   BLE adv 37 = 2402 MHz = NRF ch 2
//   BLE adv 38 = 2426 MHz = NRF ch 26
//   BLE adv 39 = 2480 MHz = NRF ch 80
static const uint8_t bleChans[] = { 2, 26, 80 };

static const uint8_t profileLen[P_COUNT] = {
  /* BLE  */ sizeof(bleChans),
  /* BT   */ 40,   // generated dynamically
  /* WiFi */ 12,
  /* FULL */ 64    // every other channel 0..125
};

static Profile profile = P_BLE;
static bool   active = false;
static bool   radioOk = false;
static uint8_t cursor = 0;
static uint32_t hopsTotal = 0;
static uint32_t lastHopUs = 0;
static uint32_t hopDwellUs = 250;  // 250us default — fast enough to clobber BT classic's 625us hops

static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;
static uint32_t lastUiMs = 0;

static uint8_t channelAt(Profile p, uint8_t idx) {
  switch (p) {
    case P_BLE:  return bleChans[idx % sizeof(bleChans)];
    case P_BT:   // 2402-2480 MHz = NRF ch 2..80, stride 2 -> 40 entries
      return (uint8_t)(2 + (idx % 40) * 2);
    case P_WIFI: {
      // 12 representative channels for WiFi 1 (NRF 1-12), 6 (NRF 27-38), 11 (NRF 52-63)
      static const uint8_t wifiChans[] = {
        1, 5, 9, 12,
        27, 30, 33, 38,
        52, 55, 58, 63
      };
      return wifiChans[idx % 12];
    }
    case P_FULL: return (uint8_t)((idx * 2) % 126);
    default:     return 0;
  }
}

static uint8_t lengthFor(Profile p) {
  return profileLen[p];
}

static const char* profileName(Profile p) {
  switch (p) {
    case P_BLE:  return "BLE-ADV (37/38/39)";
    case P_BT:   return "BT Classic (79 ch)";
    case P_WIFI: return "WiFi 1/6/11";
    case P_FULL: return "FULL 2.4 GHz sweep";
    default:     return "?";
  }
}

// Arm carrier once. Subsequent channel changes hit setChannel only — no
// stop/start cycle. That brings per-hop cost from ~300us of SPI traffic
// down to ~10us (single RF_CH register write), which is fast enough to
// actually disrupt BT classic's 625us hops.
static void carrierStart(uint8_t ch) {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setRetries(0, 0);
  radio.setPALevel(RF24_PA_MAX, true);
  radio.setDataRate(RF24_2MBPS);
  radio.setCRCLength(RF24_CRC_DISABLED);
  radio.setChannel(ch);
  radio.startConstCarrier(RF24_PA_MAX, ch);
}

static void carrierStop() {
  radio.stopConstCarrier();
  radio.powerDown();
}

static inline void carrierRetune(uint8_t ch) {
  radio.setChannel(ch);
}

static void drawScreen() {
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("NRF Mode Jammer");
  tft.setTextColor(radioOk ? TFT_GREEN : TFT_RED);
  tft.setCursor(170, 45);
  tft.print(radioOk ? "NRF OK" : "NRF FAIL");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 70);
  tft.printf("Profile: %s", profileName(profile));
  tft.setCursor(2, 85);
  tft.printf("Channels: %u", (unsigned)lengthFor(profile));
  tft.setCursor(2, 100);
  tft.printf("Hop dwell: %u us", (unsigned)hopDwellUs);
  tft.setCursor(2, 115);
  tft.printf("Now ch: %u  (%u MHz)",
             (unsigned)channelAt(profile, cursor),
             2400 + (unsigned)channelAt(profile, cursor));

  tft.setTextFont(2); tft.setTextSize(2);
  tft.setTextColor(active ? TFT_RED : TFT_DARKGREY);
  tft.setCursor(50, 145);
  tft.print(active ? "JAMMING" : "STOPPED");

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(2, 185);
  tft.printf("Hops: %u", (unsigned)hopsTotal);

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 220);
  tft.print("[UP]    Toggle TX");
  tft.setCursor(2, 235);
  tft.print("[L/R]   Cycle profile");
  tft.setCursor(2, 250);
  tft.print("[DOWN]  Cycle hop speed");
  tft.setCursor(2, 265);
  tft.setTextColor(TFT_DARKGREY);
  tft.print("[SEL]   Exit (stops TX)");
}

void modeJammerSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  profile = P_BLE;
  active = false;
  cursor = 0;
  hopsTotal = 0;
  hopDwellUs = 2000;
  toggleHeldFromPrev = true;

  // Force VSPI onto the right pin map and Hi-Z the CC1101 GDO2 output so it
  // stops driving GPIO 16 (= NRF1 CE). Same sequence Tri-Channel Watch uses;
  // it is the only known-good way to make NRF1 actually answer SPI on this
  // board after the CC1101 has been active.
  SPI.begin(18, 19, 23, 17);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  delayMicroseconds(100);
  digitalWrite(27, LOW);
  delayMicroseconds(20);
  SPI.transfer(0x00);  // W_REG IOCFG2
  SPI.transfer(0x2E);  // GDO2 -> Hi-Z, frees GPIO 16
  digitalWrite(27, HIGH);
  delayMicroseconds(50);

  // Park CE/CSN in known state before radio.begin().
  pinMode(HYDRA_NRF1_CE, OUTPUT);  digitalWrite(HYDRA_NRF1_CE, LOW);
  pinMode(HYDRA_NRF1_CSN, OUTPUT); digitalWrite(HYDRA_NRF1_CSN, HIGH);
  delay(5);

  bool ok = radio.begin();
  radioOk = ok && radio.isChipConnected();
  if (radioOk) {
    radio.stopListening();
    radio.powerDown();
  }
  Serial.printf("[ModeJam] NRF1 begin=%d connected=%d\n",
                ok, radio.isChipConnected());

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawScreen();
  lastHopUs = micros();
  lastUiMs = millis();
}

void modeJammerLoop() {
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
      if (!radioOk) {
        Serial.println("[ModeJam] NRF not responding — refusing to start");
      } else {
        active = !active;
        if (active) {
          carrierStart(channelAt(profile, cursor));
        } else {
          carrierStop();
        }
        Serial.printf("[ModeJam] %s\n", active ? "STARTED" : "STOPPED");
      }
    } else if (right) {
      bool was = active;
      if (was) { carrierStop(); active = false; }
      profile = (Profile)((profile + 1) % P_COUNT);
      cursor = 0;
      if (was) { carrierStart(channelAt(profile, cursor)); active = true; }
    } else if (left) {
      bool was = active;
      if (was) { carrierStop(); active = false; }
      profile = (Profile)((profile + P_COUNT - 1) % P_COUNT);
      cursor = 0;
      if (was) { carrierStart(channelAt(profile, cursor)); active = true; }
    } else if (down) {
      // Cycle hop dwell: 100us (max chaos) -> 250 -> 1ms -> 5ms -> 100us
      if      (hopDwellUs <= 100)    hopDwellUs = 250;
      else if (hopDwellUs <= 250)    hopDwellUs = 1000;
      else if (hopDwellUs <= 1000)   hopDwellUs = 5000;
      else                           hopDwellUs = 100;
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawScreen();
  }

  if (active) {
    uint32_t us = micros();
    if (us - lastHopUs >= hopDwellUs) {
      lastHopUs = us;
      cursor = (cursor + 1) % lengthFor(profile);
      // Retune only — carrier stays on. ~10us per hop instead of ~300us.
      carrierRetune(channelAt(profile, cursor));
      hopsTotal++;
    }
  } else {
    delay(20);
  }

  if (now - lastUiMs > 400) {
    drawScreen();
    lastUiMs = now;
  }
}

}  // namespace NrfModeJammer
