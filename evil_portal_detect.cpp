/*
 * Evil Portal Detector — Hydra. See header.
 */

#include "evil_portal_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>
#include <ctype.h>

#define DARK_GRAY 0x4208

namespace EvilPortalDetect {

// Common captive-portal SSID substrings — case-insensitive substring match
// against the broadcast SSID. Open APs with names containing any of these
// are flagged.
static const char* PORTAL_KEYWORDS[] = {
  "free wifi", "free_wifi", "freewifi",
  "guest", "public wifi", "publicwifi",
  "hotel", "airport", "lounge",
  "cafe", "starbucks", "mcdonalds",
  "xfinitywifi", "attwifi", "google starbucks",
  "captive", "portal",
  "wifi-free", "open wifi", "openwifi"
};
static const int PORTAL_KEYWORD_COUNT = sizeof(PORTAL_KEYWORDS) / sizeof(PORTAL_KEYWORDS[0]);

struct AlertRecord {
  uint8_t mac[6];
  char    essid[33];
  uint8_t channel;
  int8_t  rssi;
};

#define ALERT_QUEUE_SIZE 32
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const uint8_t* mac, const char* essid, uint8_t ch, int8_t rssi) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    int i = 0;
    while (i < 32 && essid[i]) { alertQueue[alertHead].essid[i] = essid[i]; i++; }
    alertQueue[alertHead].essid[i] = '\0';
    alertQueue[alertHead].channel = ch;
    alertQueue[alertHead].rssi = rssi;
    alertHead = nextHead;
  }
  portEXIT_CRITICAL_ISR(&alertMux);
}

static bool popAlert(AlertRecord& out) {
  bool got = false;
  portENTER_CRITICAL(&alertMux);
  if (alertTail != alertHead) {
    for (int i = 0; i < 6; i++) out.mac[i] = alertQueue[alertTail].mac[i];
    int i = 0;
    while (i < 32 && alertQueue[alertTail].essid[i]) {
      out.essid[i] = alertQueue[alertTail].essid[i]; i++;
    }
    out.essid[i] = '\0';
    out.channel = alertQueue[alertTail].channel;
    out.rssi = alertQueue[alertTail].rssi;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

static bool IRAM_ATTR ciSubstr(const char* hay, int hayLen, const char* needle) {
  int nLen = 0; while (needle[nLen]) nLen++;
  if (nLen > hayLen) return false;
  for (int i = 0; i <= hayLen - nLen; i++) {
    bool ok = true;
    for (int j = 0; j < nLen; j++) {
      char a = hay[i + j]; char b = needle[j];
      if (a >= 'A' && a <= 'Z') a += 32;
      if (b >= 'A' && b <= 'Z') b += 32;
      if (a != b) { ok = false; break; }
    }
    if (ok) return true;
  }
  return false;
}

static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 38) return;
  if (payload[0] != 0x80) return;

  // Privacy bit in capability info (offset 34, bit 4 of low byte)
  uint16_t capab = (uint16_t)payload[34] | ((uint16_t)payload[35] << 8);
  bool is_open = ((capab & 0x10) == 0);
  if (!is_open) return;

  uint8_t ssidLen = payload[37];
  if (ssidLen == 0 || ssidLen > 32) return;
  if (38 + ssidLen > pktLen) return;

  char essid[33];
  for (int i = 0; i < ssidLen; i++) {
    char c = (char)payload[38 + i];
    essid[i] = (c >= 32 && c < 127) ? c : '?';
  }
  essid[ssidLen] = '\0';

  bool keyword_hit = false;
  for (int k = 0; k < PORTAL_KEYWORD_COUNT; k++) {
    if (ciSubstr(essid, ssidLen, PORTAL_KEYWORDS[k])) { keyword_hit = true; break; }
  }
  if (!keyword_hit) return;

  pushAlert(payload + 10, essid, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi);
}

static const uint32_t HOP_DWELL_MS = 350;
static uint8_t currentChannel = 1;
static uint32_t lastHop = 0;

static void doChannelHop() {
  uint32_t now = millis();
  if (now - lastHop < HOP_DWELL_MS) return;
  currentChannel = (currentChannel % 11) + 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = now;
}

#define DEDUPE_SLOTS 32
#define DEDUPE_COOLDOWN_MS 20000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (memcmp(dedupeTable[i].mac, mac, 6) == 0) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now; return false;
    }
  }
  memcpy(dedupeTable[dedupeIdx].mac, mac, 6);
  dedupeTable[dedupeIdx].ts = now;
  dedupeIdx = (dedupeIdx + 1) % DEDUPE_SLOTS;
  return false;
}

static uint32_t totalHits = 0;
static uint32_t lastUiUpdate = 0;
#define LIST_ROWS 14
#define ROW_HEIGHT 14
#define LIST_TOP_Y 60
struct DispRow { char text[44]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static void pushDispRow(const AlertRecord& r) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x %-20s ch%d",
           r.mac[3], r.mac[4], r.mac[5], r.essid, (int)r.channel);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24); tft.print("Ch:"); tft.print(currentChannel);
  tft.setCursor(80, 24); tft.print("Portals:"); tft.print(totalHits);
  tft.setCursor(165, 24); tft.print("[SEL]=exit");
}

void evilPortalDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  totalHits = 0; dispRowCount = 0;
  alertHead = alertTail = 0; dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) { memset(dedupeTable[i].mac, 0, 6); dedupeTable[i].ts = 0; }

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Evil Portal Detector");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();
  { wifi_promiscuous_filter_t f; f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&f); }
  esp_wifi_set_promiscuous_rx_cb(&wifiSniffer);
  esp_wifi_set_promiscuous(true);

  currentChannel = 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = millis();
  Serial.println("[EvilPortal] sniffer started, hopping 1..11");

  float v = readBatteryVoltage(); drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  redrawTopStatus();
  lastUiUpdate = millis();
}

void evilPortalDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[EvilPortal] %02x:%02x:%02x:%02x:%02x:%02x essid=%s ch=%d rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  r.essid, (int)r.channel, (int)r.rssi);
    redrawList();
  }
  uint32_t now = millis();
  if (now - lastUiUpdate > 250) { redrawTopStatus(); lastUiUpdate = now; }
  delay(10);
}

}  // namespace EvilPortalDetect
