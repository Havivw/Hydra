/*
 * WiFi Pineapple Detector — Hydra port
 * See pineapple_detect.h for provenance.
 */

#include "pineapple_detect.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace PineappleDetect {

// Match flags — when an OUI hits, it only counts as suspicious if the AP's
// security mode matches what's expected for that vendor's typical use.
enum SuspicionFlag {
  SUSP_ALWAYS         = 0x01,
  SUSP_WHEN_OPEN      = 0x02,
  SUSP_WHEN_PROTECTED = 0x04
};

struct SuspectVendor {
  const char* name;
  uint8_t     flags;
  uint32_t    ouis[4];
  uint8_t     oui_count;
};

// Lifted from marauder-v1div/esp32_marauder/WiFiScan.cpp:6059
static const SuspectVendor SUSPECT_VENDORS[] = {
  {"Alfa Inc",                  SUSP_WHEN_OPEN,      {0x00C0CA},                                 1},
  {"Orient Power (Pineapple MK7)", SUSP_ALWAYS,      {0x001337},                                 1},
  {"Shenzhen Century",          SUSP_WHEN_OPEN,      {0x1CBFCE},                                 1},
  {"IEEE Reg Authority",        SUSP_WHEN_OPEN,      {0x0CEFAF},                                 1},
  {"Hak5 LAA",                  SUSP_WHEN_PROTECTED, {0x02C0CA, 0x021337},                       2},
  {"MediaTek Inc",              SUSP_ALWAYS,         {0x000A00, 0x000C43, 0x000CE7, 0x0017A5},   4},
  {"Panda Wireless",            SUSP_ALWAYS,         {0x9CEFD5, 0x9CE5D5},                       2},
  {"Unassigned/Spoofed",        SUSP_ALWAYS,         {0xDEADBE},                                 1}
};
static const int SUSPECT_VENDOR_COUNT = sizeof(SUSPECT_VENDORS) / sizeof(SUSPECT_VENDORS[0]);

// ============================================================
// HEURISTIC: minimal-beacon tag check (only DS Parameter Set after SSID)
// Some Pineapples emit beacons with capab_info=0x0001 and ONLY the DS
// channel tag after the SSID IE — fingerprint of the Hak5 firmware.
// ============================================================
static bool IRAM_ATTR onlyChannelTag(const uint8_t* payload, int len) {
  if (len < 38) return false;
  int ssid_len = payload[37];
  int pos = 36 + ssid_len + 2;  // skip header + SSID IE
  if (pos + 2 >= len) return false;
  // Next tag must be DS Parameter Set (id 3, len 1)
  if (payload[pos] != 0x03 || payload[pos + 1] != 0x01) return false;
  int after = pos + 2 + payload[pos + 1];
  return (after >= len);  // no more tags after the channel
}

// ============================================================
// ALERT QUEUE
// ============================================================
enum SuspicionReason {
  REASON_OUI       = 0,
  REASON_MIN_BCN   = 1,
  REASON_BOTH      = 2
};

struct AlertRecord {
  uint8_t  mac[6];
  uint8_t  channel;
  int8_t   rssi;
  uint8_t  reason;     // SuspicionReason
  uint8_t  vendor_idx; // index into SUSPECT_VENDORS, or 0xFF for none
  bool     is_open;
};

#define ALERT_QUEUE_SIZE 32
static volatile AlertRecord alertQueue[ALERT_QUEUE_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const uint8_t* mac, uint8_t ch, int8_t rssi,
                                       uint8_t reason, uint8_t vendor_idx, bool is_open) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t nextHead = (alertHead + 1) % ALERT_QUEUE_SIZE;
  if (nextHead != alertTail) {
    for (int i = 0; i < 6; i++) alertQueue[alertHead].mac[i] = mac[i];
    alertQueue[alertHead].channel = ch;
    alertQueue[alertHead].rssi = rssi;
    alertQueue[alertHead].reason = reason;
    alertQueue[alertHead].vendor_idx = vendor_idx;
    alertQueue[alertHead].is_open = is_open;
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
    out.reason = alertQueue[alertTail].reason;
    out.vendor_idx = alertQueue[alertTail].vendor_idx;
    out.is_open = alertQueue[alertTail].is_open;
    alertTail = (alertTail + 1) % ALERT_QUEUE_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

// ============================================================
// SNIFFER
// ============================================================
static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int pktLen = pkt->rx_ctrl.sig_len;
  if (pktLen < 36) return;

  // Only beacon frames (frame control = 0x80)
  if (payload[0] != 0x80) return;

  // Capability info at offset 34 (little endian uint16)
  uint16_t capab = (uint16_t)payload[34] | ((uint16_t)payload[35] << 8);
  bool is_protected = (capab & 0x10) != 0;
  bool is_open = !is_protected;

  uint8_t oui[3] = {payload[10], payload[11], payload[12]};
  uint32_t oui_value = ((uint32_t)oui[0] << 16) | ((uint32_t)oui[1] << 8) | oui[2];

  // OUI lookup
  int vendor_idx = 0xFF;
  bool oui_match = false;
  for (int i = 0; i < SUSPECT_VENDOR_COUNT; i++) {
    const SuspectVendor& v = SUSPECT_VENDORS[i];
    for (int j = 0; j < v.oui_count; j++) {
      if (oui_value == v.ouis[j]) {
        // Vendor OUI hit; check security-mode match
        if ((v.flags & SUSP_ALWAYS) ||
            (is_open && (v.flags & SUSP_WHEN_OPEN)) ||
            (is_protected && (v.flags & SUSP_WHEN_PROTECTED))) {
          oui_match = true;
          vendor_idx = i;
        }
        break;
      }
    }
    if (oui_match) break;
  }

  // Minimal-beacon heuristic
  bool min_bcn = (capab == 0x0001) && onlyChannelTag(payload, pktLen);

  if (!oui_match && !min_bcn) return;

  uint8_t reason = oui_match && min_bcn ? REASON_BOTH
                  : oui_match            ? REASON_OUI
                                         : REASON_MIN_BCN;

  pushAlert(payload + 10, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi,
            reason, (uint8_t)vendor_idx, is_open);
}

// ============================================================
// CHANNEL HOP (full 1..11)
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
// DEDUPE  (10s per-MAC)
// ============================================================
#define DEDUPE_SLOTS 64
#define DEDUPE_COOLDOWN_MS 10000
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

static const char* reasonShort(uint8_t r) {
  switch (r) {
    case REASON_OUI:     return "OUI";
    case REASON_MIN_BCN: return "MIN";
    case REASON_BOTH:    return "BOTH";
    default:             return "?";
  }
}

static uint16_t reasonColor(uint8_t r) {
  switch (r) {
    case REASON_BOTH:    return TFT_RED;
    case REASON_OUI:     return TFT_ORANGE;
    case REASON_MIN_BCN: return TFT_YELLOW;
    default:             return TFT_WHITE;
  }
}

static void pushDispRow(const AlertRecord& r) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  snprintf(row.text, sizeof(row.text),
           "%02x:%02x:%02x:%02x:%02x:%02x %s %d %s",
           r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
           reasonShort(r.reason), (int)r.rssi,
           r.is_open ? "OPEN" : "WPA");
  row.color = reasonColor(r.reason);
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
void pineappleDetectSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  totalHits = 0;
  dispRowCount = 0;
  alertHead = alertTail = 0;
  dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    memset(dedupeTable[i].mac, 0, 6);
    dedupeTable[i].ts = 0;
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] Pineapple Detector");
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

  Serial.println("[Pineapple] sniffer started, hopping 1..11");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  redrawTopStatus();
  lastUiUpdate = millis();
}

void pineappleDetectLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  AlertRecord r;
  int drained = 0;
  while (drained < 4 && popAlert(r)) {
    drained++;
    if (shouldSuppress(r.mac)) continue;
    totalHits++;
    pushDispRow(r);
    const char* vendor = (r.vendor_idx < SUSPECT_VENDOR_COUNT)
                          ? SUSPECT_VENDORS[r.vendor_idx].name : "(none)";
    Serial.printf("[Pineapple] %02x:%02x:%02x:%02x:%02x:%02x %s vendor=%s ch=%d rssi=%d\n",
                  r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
                  reasonShort(r.reason), vendor, (int)r.channel, (int)r.rssi);
    redrawList();
  }

  uint32_t now = millis();
  if (now - lastUiUpdate > 250) {
    redrawTopStatus();
    lastUiUpdate = now;
  }

  delay(10);
}

}  // namespace PineappleDetect
