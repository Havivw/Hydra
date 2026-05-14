/*
 * Pwnagotchi Detector — Hydra port
 * See pwn_detect.h for provenance.
 */

#include "pwn_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace PwnDetect {

// Pwnagotchi devices broadcast beacons sourced from this exact MAC.
static const uint8_t PWN_MAC[6] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};

// ============================================================
// ALERT QUEUE
// ============================================================
struct AlertRecord {
  char    name[33];   // pwnagotchi name (max ~32)
  uint8_t channel;
  int8_t  rssi;
};

#define ALERT_QUEUE_SIZE 16
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const char* name, uint8_t ch, int8_t rssi) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    int i = 0;
    while (i < 32 && name[i]) { alertQueue[alertHead].name[i] = name[i]; i++; }
    alertQueue[alertHead].name[i] = '\0';
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
    int i = 0;
    while (i < 32 && alertQueue[alertTail].name[i]) { out.name[i] = alertQueue[alertTail].name[i]; i++; }
    out.name[i] = '\0';
    out.channel = alertQueue[alertTail].channel;
    out.rssi = alertQueue[alertTail].rssi;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

// ============================================================
// CALLBACK + JSON NAME EXTRACT
// ============================================================
// Extract "name":"<value>" from a JSON-ish blob, in IRAM. Bounded; no malloc.
// Returns bytes copied to out (excluding null) or 0 if not found.
static int IRAM_ATTR extractPwnName(const uint8_t* buf, int len, char* out, int outCap) {
  // Find first '{' to locate JSON start
  int jsonStart = -1;
  for (int i = 0; i < len; i++) {
    if (buf[i] == '{') { jsonStart = i; break; }
  }
  if (jsonStart < 0) return 0;

  // Search for the literal token: "name":"
  static const char tok[] = "\"name\":\"";
  const int tokLen = 8;
  int matchStart = -1;
  for (int i = jsonStart; i <= len - tokLen; i++) {
    bool ok = true;
    for (int j = 0; j < tokLen; j++) {
      if (buf[i + j] != (uint8_t)tok[j]) { ok = false; break; }
    }
    if (ok) { matchStart = i + tokLen; break; }
  }
  if (matchStart < 0) return 0;

  // Copy until closing quote, capped at outCap-1
  int o = 0;
  for (int i = matchStart; i < len && o < outCap - 1; i++) {
    if (buf[i] == '"') break;
    out[o++] = (char)buf[i];
  }
  out[o] = '\0';
  return o;
}

static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 36) return;

  // Beacon frame check (frame control byte 0x80)
  if (payload[0] != 0x80) return;

  // Source MAC at offset 10. Compare against Pwnagotchi sentinel.
  for (int i = 0; i < 6; i++) {
    if (payload[10 + i] != PWN_MAC[i]) return;
  }

  // Extract name from embedded JSON
  char name[33];
  int nameLen = extractPwnName(payload, pktLen, name, sizeof(name));
  if (nameLen == 0) {
    strncpy(name, "(no-name)", sizeof(name));
  }

  pushAlert(name, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi);
}

// ============================================================
// CHANNEL HOP (full 1..11 sweep, 350ms dwell)
// ============================================================
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

// ============================================================
// DEDUPE  (60s per-NAME to allow Pwnagotchis to re-surface periodically)
// ============================================================
#define DEDUPE_SLOTS 16
#define DEDUPE_COOLDOWN_MS 60000
struct DedupeEntry { char name[33]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const char* name) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (strncmp(dedupeTable[i].name, name, 32) == 0) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now;
      return false;
    }
  }
  strncpy(dedupeTable[dedupeIdx].name, name, 32);
  dedupeTable[dedupeIdx].name[32] = '\0';
  dedupeTable[dedupeIdx].ts = now;
  dedupeIdx = (dedupeIdx + 1) % DEDUPE_SLOTS;
  return false;
}

// ============================================================
// DISPLAY
// ============================================================
static uint32_t totalHits = 0;
static uint32_t lastUiUpdate = 0;

#define LIST_ROWS 14
#define ROW_HEIGHT 14
#define LIST_TOP_Y 60
struct DispRow { char text[40]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static void pushDispRow(const AlertRecord& r) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text), "%-16s  %d  ch=%d", r.name, (int)r.rssi, (int)r.channel);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.print("Ch:");
  tft.print(currentChannel);
  tft.setCursor(80, 24);
  tft.print("Hits:");
  tft.print(totalHits);
  tft.setCursor(165, 24);
  tft.print("[SEL]=exit");
}

// ============================================================
// PUBLIC API
// ============================================================
void pwnDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  totalHits = 0;
  dispRowCount = 0;
  alertHead = alertTail = 0;
  dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    dedupeTable[i].name[0] = '\0';
    dedupeTable[i].ts = 0;
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Pwnagotchi Detector");
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

  Serial.println("[Pwn] sniffer started, full-channel hop");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  redrawTopStatus();
  lastUiUpdate = millis();
}

void pwnDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.name)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[Pwn] name=%s ch=%d rssi=%d\n", r.name, (int)r.channel, (int)r.rssi);
    redrawList();
  }

  uint32_t now = millis();
  if (now - lastUiUpdate > 250) {
    redrawTopStatus();
    lastUiUpdate = now;
  }

  delay(10);
}

}  // namespace PwnDetect
