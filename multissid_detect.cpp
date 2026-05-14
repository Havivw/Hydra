/*
 * Multi-SSID Detector — Hydra. See header.
 */

#include "multissid_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace MultiSsidDetect {

// Track up to 64 BSSIDs, each with up to 8 distinct SSIDs seen. When count
// exceeds threshold, push an alert.
#define MAX_TRACKED 8
#define MAX_SSIDS_PER_BSSID 3
#define MULTI_THRESHOLD 3

struct TrackerEntry {
  uint8_t  mac[6];
  uint8_t  ssidCount;
  char     ssids[MAX_SSIDS_PER_BSSID][33];
  bool     reported;
  int8_t   bestRssi;
  uint8_t  channel;
};

static TrackerEntry trackers[MAX_TRACKED];
static int trackerCount = 0;
static portMUX_TYPE trackerMux = portMUX_INITIALIZER_UNLOCKED;

struct AlertRecord {
  uint8_t  mac[6];
  uint8_t  ssidCount;
  int8_t   rssi;
  uint8_t  channel;
};

#define ALERT_QUEUE_SIZE 16
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const TrackerEntry& t) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = t.mac[i];
    alertQueue[alertHead].ssidCount = t.ssidCount;
    alertQueue[alertHead].rssi = t.bestRssi;
    alertQueue[alertHead].channel = t.channel;
    alertHead = nextHead;
  }
  portEXIT_CRITICAL_ISR(&alertMux);
}

static bool popAlert(AlertRecord& out) {
  bool got = false;
  portENTER_CRITICAL(&alertMux);
  if (alertTail != alertHead) {
    for (int i = 0; i < 6; i++) out.mac[i] = alertQueue[alertTail].mac[i];
    out.ssidCount = alertQueue[alertTail].ssidCount;
    out.rssi = alertQueue[alertTail].rssi;
    out.channel = alertQueue[alertTail].channel;
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
  if (pktLen < 38) return;
  if (payload[0] != 0x80) return;

  uint8_t ssidLen = payload[37];
  if (ssidLen == 0 || ssidLen > 32) return;
  if (38 + ssidLen > pktLen) return;

  char essid[33];
  for (int i = 0; i < ssidLen; i++) essid[i] = (char)payload[38 + i];
  essid[ssidLen] = '\0';

  const uint8_t* mac = payload + 10;

  portENTER_CRITICAL_ISR(&trackerMux);
  int idx = -1;
  for (int i = 0; i < trackerCount; i++) {
    if (memcmp(trackers[i].mac, mac, 6) == 0) { idx = i; break; }
  }

  if (idx < 0) {
    if (trackerCount >= MAX_TRACKED) {
      // Reset the table when full — keeps us looking forward.
      trackerCount = 0;
    }
    idx = trackerCount++;
    memcpy(trackers[idx].mac, mac, 6);
    trackers[idx].ssidCount = 0;
    trackers[idx].reported = false;
    trackers[idx].bestRssi = -128;
    trackers[idx].channel = pkt->rx_ctrl.channel;
  }

  // Have we seen this SSID before from this BSSID?
  bool seen = false;
  for (int j = 0; j < trackers[idx].ssidCount; j++) {
    if (strncmp(trackers[idx].ssids[j], essid, 32) == 0) { seen = true; break; }
  }
  if (!seen && trackers[idx].ssidCount < MAX_SSIDS_PER_BSSID) {
    strncpy(trackers[idx].ssids[trackers[idx].ssidCount], essid, 32);
    trackers[idx].ssids[trackers[idx].ssidCount][32] = '\0';
    trackers[idx].ssidCount++;
  }

  if (pkt->rx_ctrl.rssi > trackers[idx].bestRssi) trackers[idx].bestRssi = pkt->rx_ctrl.rssi;
  trackers[idx].channel = pkt->rx_ctrl.channel;

  // Cross the threshold once → emit alert
  if (!trackers[idx].reported && trackers[idx].ssidCount >= MULTI_THRESHOLD) {
    trackers[idx].reported = true;
    TrackerEntry snapshot = trackers[idx];
    portEXIT_CRITICAL_ISR(&trackerMux);
    pushAlert(snapshot);
    return;
  }
  portEXIT_CRITICAL_ISR(&trackerMux);
}

static const uint32_t HOP_DWELL_MS = 500;
static uint8_t currentChannel = 1;
static uint32_t lastHop = 0;

static void doChannelHop() {
  uint32_t now = millis();
  if (now - lastHop < HOP_DWELL_MS) return;
  currentChannel = (currentChannel % 11) + 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = now;
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
           "%02x:%02x:%02x:%02x:%02x:%02x  %d SSIDs  ch%d",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
           (int)r.ssidCount, (int)r.channel);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
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
  tft.setCursor(80, 24); tft.print("Multi:"); tft.print(totalHits);
  tft.setCursor(165, 24); tft.print("[SEL]=exit");
}

void multiSsidDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  totalHits = 0; dispRowCount = 0;
  alertHead = alertTail = 0;
  trackerCount = 0;

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Multi-SSID Hotspot Detector");
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
  Serial.println("[MultiSSID] sniffer started, hopping 1..11, threshold=3 SSIDs per BSSID");

  float v = readBatteryVoltage(); drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  redrawTopStatus();
  lastUiUpdate = millis();
}

void multiSsidDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[MultiSSID] %02x:%02x:%02x:%02x:%02x:%02x ssid_count=%d ch=%d rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  (int)r.ssidCount, (int)r.channel, (int)r.rssi);
    redrawList();
  }
  uint32_t now = millis();
  if (now - lastUiUpdate > 250) { redrawTopStatus(); lastUiUpdate = now; }
  delay(10);
}

}  // namespace MultiSsidDetect
