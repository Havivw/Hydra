/*
 * Flock Detector — Hydra port
 * See flock_detect.h for provenance and credit.
 */

#include "flock_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

// drawStatusBar is declared in utils.h. runUI/updateStatusBar/uiDrawn in
// cifertech are per-namespace internals (each feature defines its own);
// we don't depend on those here — redrawTopStatus() below is FlockDetect's
// own minimal status line and the battery icon will be added when the
// shared helpers get factored out of per-feature namespaces.

#define DARK_GRAY 0x4208

namespace FlockDetect {

// ============================================================
// TARGET OUI LIST  (lowercase, colons). Lifted verbatim from
// flock-you main.cpp lines 84-95 (promiscious-dev branch).
// ============================================================
static const char* target_ouis[] = {
  "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "b8:35:32",
  "14:5a:fc", "74:4c:a1", "08:3a:88", "9c:2f:9d", "c0:35:32",
  "94:08:53", "e4:aa:ea", "f4:6a:dd", "f8:a2:d6", "24:b2:b9",
  "00:f4:8d", "d0:39:57", "e8:d0:fc", "e0:4f:43", "b8:1e:a4",
  "70:08:94", "58:8e:81", "ec:1b:bd", "3c:71:bf", "58:00:e3",
  "90:35:ea", "5c:93:a2", "64:6e:69", "48:27:ea", "a4:cf:12",
  "82:6b:f2"  // DeFlockJoplin's 31st — wildcard-probe field discovery
};
static const size_t OUI_COUNT = sizeof(target_ouis) / sizeof(target_ouis[0]);

// Pre-compiled byte table — populated once, never touched again.
static uint8_t oui_bytes[OUI_COUNT][3];

// ============================================================
// ALERT QUEUE (sniffer callback → loop context)
// ============================================================
enum AlertKind {
  ALERT_OUI_ADDR2 = 0,
  ALERT_OUI_ADDR1 = 1,
  ALERT_OUI_ADDR3 = 2,
  ALERT_WILDCARD_PROBE = 3
};

struct AlertRecord {
  uint8_t  mac[6];
  uint8_t  channel;
  int8_t   rssi;
  uint8_t  kind;
};

#define ALERT_QUEUE_SIZE 32
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const uint8_t* mac, uint8_t ch, int8_t rssi, uint8_t kind) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {  // not full
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    alertQueue[alertHead].channel = ch;
    alertQueue[alertHead].rssi = rssi;
    alertQueue[alertHead].kind = kind;
    alertHead = nextHead;
  }
  portEXIT_CRITICAL_ISR(&alertMux);
}

static bool popAlert(AlertRecord& out) {
  bool got = false;
  portENTER_CRITICAL(&alertMux);
  if (alertTail != alertHead) {
    for (int i = 0; i < 6; i++) out.mac[i] = alertQueue[alertTail].mac[i];
    out.channel = alertQueue[alertTail].channel;
    out.rssi = alertQueue[alertTail].rssi;
    out.kind = alertQueue[alertTail].kind;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

// ============================================================
// MATCH HELPERS
// ============================================================
static inline bool IRAM_ATTR isMulticast(const uint8_t* mac) {
  return mac[0] & 0x01;
}

static bool IRAM_ATTR matchOuiRaw(const uint8_t* mac) {
  // Locally-administered (randomised) MACs have bit 1 of byte 0 set.
  // Fixed infrastructure devices never use them.
  if (mac[0] & 0x02) return false;
  for (size_t i = 0; i < OUI_COUNT; i++) {
    if (mac[0] == oui_bytes[i][0] &&
        mac[1] == oui_bytes[i][1] &&
        mac[2] == oui_bytes[i][2]) return true;
  }
  return false;
}

// ============================================================
// PROMISCUOUS CALLBACK
// ============================================================
static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 24) return;

  uint8_t frameControl0 = payload[0];
  uint8_t frameType    = (frameControl0 >> 2) & 0x03;
  uint8_t frameSubtype = (frameControl0 >> 4) & 0x0F;

  const uint8_t* addr1 = payload + 4;
  const uint8_t* addr2 = payload + 10;
  const uint8_t* addr3 = payload + 16;

  uint8_t channel = pkt->rx_ctrl.channel;
  int8_t  rssi    = pkt->rx_ctrl.rssi;

  // Wildcard probe-request signature (DeFlockJoplin):
  //   Management frame (type 0), subtype 4 (Probe Request), SSID IE tag 0
  //   with length 0, and transmitter (addr2) matches a known OUI.
  if (frameType == 0 && frameSubtype == 4 && pktLen >= 26) {
    const uint8_t* ie = payload + 24;
    if (ie[0] == 0x00 && ie[1] == 0x00 && matchOuiRaw(addr2)) {
      pushAlert(addr2, channel, rssi, ALERT_WILDCARD_PROBE);
      return;
    }
  }

  // Standard OUI matches on each address slot. Skip multicast/broadcast.
  if (!isMulticast(addr2) && matchOuiRaw(addr2)) {
    pushAlert(addr2, channel, rssi, ALERT_OUI_ADDR2);
    return;
  }
  if (!isMulticast(addr1) && matchOuiRaw(addr1)) {
    pushAlert(addr1, channel, rssi, ALERT_OUI_ADDR1);
    return;
  }
  if (!isMulticast(addr3) && matchOuiRaw(addr3)) {
    pushAlert(addr3, channel, rssi, ALERT_OUI_ADDR3);
    return;
  }
}

// ============================================================
// CHANNEL HOP
// ============================================================
static const uint8_t HOP_CHANNELS[] = {1, 6, 11};
static const uint8_t HOP_COUNT = sizeof(HOP_CHANNELS) / sizeof(HOP_CHANNELS[0]);
static const uint32_t HOP_DWELL_MS = 350;
static uint8_t hopIndex = 0;
static uint8_t currentChannel = 1;
static uint32_t lastHop = 0;

static void doChannelHop() {
  uint32_t now = millis();
  if (now - lastHop < HOP_DWELL_MS) return;
  hopIndex = (hopIndex + 1) % HOP_COUNT;
  currentChannel = HOP_CHANNELS[hopIndex];
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = now;
}

// ============================================================
// DEDUPE  (5s per-MAC rate limit for display + serial spam control)
// ============================================================
#define DEDUPE_SLOTS 64
#define DEDUPE_COOLDOWN_MS 5000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool macEq(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (macEq(dedupeTable[i].mac, mac)) {
      if ((now - dedupeTable[i].ts) < DEDUPE_COOLDOWN_MS) return true;
      dedupeTable[i].ts = now;
      return false;
    }
  }
  // Not found — insert in rotating slot
  memcpy(dedupeTable[dedupeIdx].mac, mac, 6);
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
struct DispRow { char text[40]; uint16_t color; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static const char* kindName(uint8_t k) {
  switch (k) {
    case ALERT_OUI_ADDR2: return "OUI a2";
    case ALERT_OUI_ADDR1: return "OUI a1";
    case ALERT_OUI_ADDR3: return "OUI a3";
    case ALERT_WILDCARD_PROBE: return "PROBE*";
    default: return "??";
  }
}

static uint16_t kindColor(uint8_t k) {
  switch (k) {
    case ALERT_WILDCARD_PROBE: return TFT_RED;       // high confidence
    case ALERT_OUI_ADDR2:      return TFT_ORANGE;
    case ALERT_OUI_ADDR1:      return TFT_YELLOW;
    case ALERT_OUI_ADDR3:      return TFT_CYAN;
    default:                   return TFT_WHITE;
  }
}

static void pushDispRow(const AlertRecord& r) {
  // Scroll up — drop the oldest if list is full.
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x:%02x:%02x:%02x %s %d %d",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
           kindName(r.kind), (int)r.rssi, (int)r.channel);
  row.color = kindColor(r.kind);
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setTextColor(dispRows[i].color, TFT_BLACK);
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
void flockDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  // Precompile OUI byte table
  for (size_t i = 0; i < OUI_COUNT; i++) {
    const char* o = target_ouis[i];
    oui_bytes[i][0] = (uint8_t)strtol(o,     nullptr, 16);
    oui_bytes[i][1] = (uint8_t)strtol(o + 3, nullptr, 16);
    oui_bytes[i][2] = (uint8_t)strtol(o + 6, nullptr, 16);
  }

  // Reset state on each entry into the feature
  totalHits = 0;
  dispRowCount = 0;
  alertHead = alertTail = 0;
  dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    memset((void*)dedupeTable[i].mac, 0, 6);
    dedupeTable[i].ts = 0;
  }

  // Header text on splash
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Flock Detector — passive scan");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Bring WiFi up in promiscuous-only mode
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

  hopIndex = 0;
  currentChannel = HOP_CHANNELS[0];
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = millis();

  Serial.println("[Flock] sniffer started, hopping 1/6/11");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  redrawTopStatus();
  lastUiUpdate = millis();
}

void flockDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  // Hop channel when dwell expires
  doChannelHop();

  // Drain alert queue — at most a few per call to keep UI responsive
  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    Serial.printf("[Flock] %02x:%02x:%02x:%02x:%02x:%02x  %s  ch=%d  rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  kindName(r.kind), (int)r.channel, (int)r.rssi);
    redrawList();
  }

  // Refresh top status every ~250ms (channel updates while idle)
  uint32_t now = millis();
  if (now - lastUiUpdate > 250) {
    redrawTopStatus();
    lastUiUpdate = now;
  }

  delay(10);
}

}  // namespace FlockDetect
