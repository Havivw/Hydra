/*
 * Probe Request Sniffer — Hydra port. See header.
 */

#include "probe_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace ProbeDetect {

struct AlertRecord {
  uint8_t mac[6];
  char    essid[33];  // SSID max 32 chars
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

static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 26) return;

  // Probe Request: frame control byte 0x40
  if (payload[0] != 0x40) return;

  // SSID Information Element at offset 24: tag(0x00), len, then SSID bytes
  uint8_t tag = payload[24];
  if (tag != 0x00) return;
  uint8_t ssidLen = payload[25];
  if (ssidLen > 32) ssidLen = 32;

  char essid[33];
  if (ssidLen == 0) {
    strncpy(essid, "(wildcard)", sizeof(essid));
  } else {
    if (26 + ssidLen > pktLen) return;
    for (int i = 0; i < ssidLen; i++) {
      char c = (char)payload[26 + i];
      essid[i] = (c >= 32 && c < 127) ? c : '?';  // strip non-printable
    }
    essid[ssidLen] = '\0';
  }

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

// Dedupe by MAC+SSID combo so the same phone asking for the same network
// doesn't flood. Keep cooldown short so we still capture variety.
#define DEDUPE_SLOTS 24
#define DEDUPE_COOLDOWN_MS 8000
struct DedupeEntry { uint8_t mac[6]; char essid[33]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const uint8_t* mac, const char* essid) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (memcmp(dedupeTable[i].mac, mac, 6) == 0 &&
        strncmp(dedupeTable[i].essid, essid, 32) == 0) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now;
      return false;
    }
  }
  memcpy(dedupeTable[dedupeIdx].mac, mac, 6);
  strncpy(dedupeTable[dedupeIdx].essid, essid, 32);
  dedupeTable[dedupeIdx].essid[32] = '\0';
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
  // Show last 3 MAC bytes + essid (truncated if long) + rssi
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x %-22s %d",
           r.mac[3], r.mac[4], r.mac[5], r.essid, (int)r.rssi);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.print("Ch:");
  tft.print(currentChannel);
  tft.setCursor(80, 24);
  tft.print("Probes:");
  tft.print(totalHits);
  tft.setCursor(165, 24);
  tft.print("[SEL]=exit");
}

void probeDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  totalHits = 0; dispRowCount = 0;
  alertHead = alertTail = 0; dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    memset(dedupeTable[i].mac, 0, 6);
    dedupeTable[i].essid[0] = '\0';
    dedupeTable[i].ts = 0;
  }

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Probe Request Sniffer");
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
  Serial.println("[Probe] sniffer started, hopping 1..11");

  float v = readBatteryVoltage(); drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  redrawTopStatus();
  lastUiUpdate = millis();
}

void probeDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac, r.essid)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[Probe] %02x:%02x:%02x:%02x:%02x:%02x essid=%s ch=%d rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  r.essid, (int)r.channel, (int)r.rssi);
    redrawList();
  }

  uint32_t now = millis();
  if (now - lastUiUpdate > 250) { redrawTopStatus(); lastUiUpdate = now; }
  delay(10);
}

}  // namespace ProbeDetect
