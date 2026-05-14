/*
 * Camera Scanner — see header.
 */

#include "camera_scanner.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

namespace CameraScanner {

// ============================================================
// CAMERA VENDOR OUI LIST
// ============================================================
// Curated set of 3-byte MAC prefixes belonging to common WiFi camera makers.
// Compiled from public IEEE OUI registrations and known camera-vendor
// allocations. Not exhaustive — feel free to extend.
struct CameraOui {
  uint8_t prefix[3];
  const char* vendor;
};

static const CameraOui CAMERA_OUIS[] = {
  // Hikvision
  {{0x00, 0x40, 0x7F}, "Hikvision"},
  {{0x28, 0x57, 0xBE}, "Hikvision"},
  {{0x44, 0x19, 0xB6}, "Hikvision"},
  {{0x4C, 0xBD, 0x8F}, "Hikvision"},
  {{0x88, 0x0D, 0x8B}, "Hikvision"},
  {{0xBC, 0xAD, 0x28}, "Hikvision"},
  {{0xC0, 0x51, 0x7E}, "Hikvision"},
  {{0xC0, 0x56, 0xE3}, "Hikvision"},
  // Dahua / Lorex / Amcrest (same OEM)
  {{0x14, 0xA7, 0x8B}, "Dahua"},
  {{0x38, 0xAF, 0x29}, "Dahua"},
  {{0x3C, 0xEF, 0x8C}, "Dahua"},
  {{0x4C, 0x11, 0xBF}, "Dahua"},
  {{0x64, 0x42, 0x84}, "Dahua"},
  {{0x9C, 0x14, 0x63}, "Dahua"},
  {{0xA0, 0xBD, 0x1D}, "Dahua"},
  {{0x9C, 0x8E, 0xCD}, "Amcrest"},
  // Reolink
  {{0xD4, 0xF5, 0xEF}, "Reolink"},
  {{0xDC, 0x54, 0x6B}, "Reolink"},
  {{0xEC, 0x71, 0xDB}, "Reolink"},
  // Nest (Google)
  {{0x18, 0xB4, 0x30}, "Nest"},
  {{0x64, 0x16, 0x66}, "Nest"},
  // Ring (Amazon)
  {{0xB0, 0x09, 0xDA}, "Ring"},
  {{0xB0, 0x7F, 0xB9}, "Ring"},
  {{0xFC, 0xA1, 0x83}, "Ring"},
  // Wyze
  {{0x2C, 0xAA, 0x8E}, "Wyze"},
  {{0x7C, 0x78, 0xB2}, "Wyze"},
  {{0xA4, 0xDA, 0x32}, "Wyze"},
  // Eufy / Anker
  {{0x8C, 0x85, 0x80}, "Eufy"},
  {{0x8C, 0x71, 0xF8}, "Eufy"},
  // Foscam
  {{0x00, 0x1B, 0x11}, "Foscam"},
  {{0x0C, 0xDD, 0x24}, "Foscam"},
  // Arlo / Netgear
  {{0x00, 0x08, 0xCA}, "Arlo"},
  {{0x50, 0xF1, 0x4A}, "Arlo"},
  {{0x4C, 0x60, 0xDE}, "Arlo"},
  // Axis
  {{0x00, 0x40, 0x8C}, "Axis"},
  {{0xAC, 0xCC, 0x8E}, "Axis"},
  {{0xB8, 0xA4, 0x4F}, "Axis"},
  // Hanwha / Samsung Pro
  {{0x00, 0x09, 0x18}, "Hanwha"},
  // TP-Link Tapo / VIGI cameras
  {{0x14, 0xEB, 0xB6}, "TP-Link"},
  {{0x3C, 0x52, 0xA1}, "TP-Link"},
  {{0xB0, 0x4E, 0x26}, "TP-Link"},
  // Ubiquiti UniFi cameras
  {{0x24, 0x5A, 0x4C}, "Ubiquiti"},
  {{0xF0, 0x9F, 0xC2}, "Ubiquiti"},
  {{0x68, 0x72, 0x51}, "Ubiquiti"},
  // Logitech Circle / webcams
  {{0x00, 0x1F, 0x5B}, "Logitech"},
};
static const int CAMERA_OUI_COUNT = sizeof(CAMERA_OUIS) / sizeof(CAMERA_OUIS[0]);

static const char* matchVendor(const uint8_t* mac) {
  // Locally-administered MACs (bit 1 of byte 0 set) are randomised and
  // never match infrastructure devices — skip immediately.
  if (mac[0] & 0x02) return nullptr;
  for (int i = 0; i < CAMERA_OUI_COUNT; i++) {
    if (mac[0] == CAMERA_OUIS[i].prefix[0] &&
        mac[1] == CAMERA_OUIS[i].prefix[1] &&
        mac[2] == CAMERA_OUIS[i].prefix[2]) {
      return CAMERA_OUIS[i].vendor;
    }
  }
  return nullptr;
}

// ============================================================
// ALERT QUEUE
// ============================================================
struct Alert {
  uint8_t  mac[6];
  uint8_t  bssid[6];
  const char* vendor;
  uint8_t  channel;
  int8_t   rssi;
};

#define ALERT_Q_SIZE 16
static volatile Alert alertQ[ALERT_Q_SIZE];
static volatile uint8_t alertHead = 0;
static volatile uint8_t alertTail = 0;
static portMUX_TYPE alertMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR pushAlert(const uint8_t* mac, const uint8_t* bssid,
                                       const char* vendor, uint8_t ch, int8_t rssi) {
  portENTER_CRITICAL_ISR(&alertMux);
  uint8_t next = (alertHead + 1) % ALERT_Q_SIZE;
  if (next != alertTail) {
    for (int i = 0; i < 6; i++) {
      alertQ[alertHead].mac[i] = mac[i];
      alertQ[alertHead].bssid[i] = bssid[i];
    }
    alertQ[alertHead].vendor = vendor;
    alertQ[alertHead].channel = ch;
    alertQ[alertHead].rssi = rssi;
    alertHead = next;
  }
  portEXIT_CRITICAL_ISR(&alertMux);
}

static bool popAlert(Alert& out) {
  bool got = false;
  portENTER_CRITICAL(&alertMux);
  if (alertTail != alertHead) {
    for (int i = 0; i < 6; i++) {
      out.mac[i] = alertQ[alertTail].mac[i];
      out.bssid[i] = alertQ[alertTail].bssid[i];
    }
    out.vendor = alertQ[alertTail].vendor;
    out.channel = alertQ[alertTail].channel;
    out.rssi = alertQ[alertTail].rssi;
    alertTail = (alertTail + 1) % ALERT_Q_SIZE;
    got = true;
  }
  portEXIT_CRITICAL(&alertMux);
  return got;
}

// ============================================================
// PROMISC CALLBACK — parses ToDS/FromDS to identify BSSID vs station
// ============================================================
static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_DATA) return;  // data frames carry STA<->AP traffic
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;

  uint8_t fc0 = p[0];
  uint8_t fc1 = p[1];
  uint8_t frameType = (fc0 >> 2) & 0x3;
  if (frameType != 2) return;  // type 2 = data
  bool toDS   = (fc1 >> 0) & 0x1;
  bool fromDS = (fc1 >> 1) & 0x1;
  if (toDS && fromDS) return;  // WDS / mesh — skip (BSSID isn't unambiguous)

  const uint8_t* a1 = p + 4;
  const uint8_t* a2 = p + 10;
  const uint8_t* a3 = p + 16;

  const uint8_t* bssid = nullptr;
  const uint8_t* sta   = nullptr;
  if (!toDS && !fromDS)      { bssid = a3; sta = a2; }
  else if (!toDS && fromDS)  { bssid = a2; sta = a1; }   // AP -> STA, STA = a1
  else                       { bssid = a1; sta = a2; }   // STA -> AP, STA = a2

  if (!sta || !bssid) return;
  // Skip multicast/broadcast in either slot
  if ((sta[0] & 0x01) || (bssid[0] & 0x01)) return;

  const char* vendor = matchVendor(sta);
  if (!vendor) {
    // Sometimes the station is at a1 even in our normal cases — try alt
    if (!fromDS) return;  // already checked a1 in the FromDS branch
    vendor = matchVendor(a1);
    if (!vendor) return;
    sta = a1;
  }

  pushAlert(sta, bssid, vendor, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi);
}

// ============================================================
// CHANNEL HOP
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
// PER-MAC DEDUPE  (10s cooldown so the same camera doesn't spam)
// ============================================================
#define DEDUPE_SLOTS 24
#define DEDUPE_COOLDOWN_MS 10000
struct DedupeEntry { uint8_t mac[6]; uint32_t ts; };
static DedupeEntry dedupeTable[DEDUPE_SLOTS];
static uint8_t dedupeIdx = 0;

static bool shouldSuppress(const uint8_t* mac) {
  uint32_t now = millis();
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    if (memcmp(dedupeTable[i].mac, mac, 6) == 0) {
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
// PER-CAMERA TABLE (rolling)
// ============================================================
struct CamRow {
  uint8_t     mac[6];
  uint8_t     bssid[6];
  const char* vendor;
  uint8_t     channel;
  int8_t      rssi;
  char        ssid[33];   // resolved from TargetList if known
};
#define MAX_CAMS 12
static CamRow cams[MAX_CAMS];
static int    camCount = 0;
static uint32_t totalHits = 0;

static const char* lookupSsid(const uint8_t* bssid) {
  for (int i = 0; i < TargetList::targetCount; i++) {
    if (memcmp(TargetList::targets[i].bssid, bssid, 6) == 0) {
      return TargetList::targets[i].ssid;
    }
  }
  return nullptr;
}

static void recordCamera(const Alert& a) {
  // If already known, refresh RSSI/channel
  for (int i = 0; i < camCount; i++) {
    if (memcmp(cams[i].mac, a.mac, 6) == 0) {
      cams[i].rssi = a.rssi;
      cams[i].channel = a.channel;
      memcpy(cams[i].bssid, a.bssid, 6);
      const char* s = lookupSsid(a.bssid);
      if (s) { strncpy(cams[i].ssid, s, 32); cams[i].ssid[32] = '\0'; }
      else   { strncpy(cams[i].ssid, "(unknown)", sizeof(cams[i].ssid)); }
      return;
    }
  }
  if (camCount < MAX_CAMS) {
    CamRow& c = cams[camCount++];
    memcpy(c.mac, a.mac, 6);
    memcpy(c.bssid, a.bssid, 6);
    c.vendor = a.vendor;
    c.channel = a.channel;
    c.rssi = a.rssi;
    const char* s = lookupSsid(a.bssid);
    if (s) { strncpy(c.ssid, s, 32); c.ssid[32] = '\0'; }
    else   { strncpy(c.ssid, "(unknown)", sizeof(c.ssid)); }
  }
}

// ============================================================
// DISPLAY
// ============================================================
static uint32_t lastUiUpdate = 0;
#define LIST_TOP_Y 70
#define ROW_HEIGHT 16

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 30, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Camera Scanner  ch%d  hits:%u",
             (int)currentChannel, (unsigned)totalHits);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 58);
  tft.printf("Run AP Scan first to resolve SSIDs (%d in cache)",
             TargetList::targetCount);
}

static void drawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y - 10, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  if (camCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, LIST_TOP_Y + 10);
    tft.print("(no cameras detected yet)");
    return;
  }
  for (int i = 0; i < camCount; i++) {
    int y = LIST_TOP_Y + i * ROW_HEIGHT;
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf("%-9s %02x:%02x:%02x:%02x:%02x:%02x ch%-2d %d",
               cams[i].vendor,
               cams[i].mac[0], cams[i].mac[1], cams[i].mac[2],
               cams[i].mac[3], cams[i].mac[4], cams[i].mac[5],
               (int)cams[i].channel, (int)cams[i].rssi);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char ssid_show[20];
    int sl = strlen(cams[i].ssid);
    if (sl > 19) sl = 19;
    memcpy(ssid_show, cams[i].ssid, sl);
    ssid_show[sl] = '\0';
    tft.setCursor(140, y);
    tft.printf("@%s", ssid_show);
  }
}

void cameraScannerSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  camCount = 0;
  totalHits = 0;
  alertHead = alertTail = 0;
  dedupeIdx = 0;
  for (int i = 0; i < DEDUPE_SLOTS; i++) {
    memset(dedupeTable[i].mac, 0, 6);
    dedupeTable[i].ts = 0;
  }

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

  Serial.printf("[CamScan] %d OUIs loaded; TargetList has %d APs\n",
                CAMERA_OUI_COUNT, TargetList::targetCount);

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawHeader();
  drawList();
  lastUiUpdate = millis();
}

void cameraScannerLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  doChannelHop();

  Alert a;
  int drained = 0;
  bool changed = false;
  while (drained < 4 && popAlert(a)) {
    drained++;
    if (shouldSuppress(a.mac)) continue;
    totalHits++;
    recordCamera(a);
    Serial.printf("[CamScan] %s %02x:%02x:%02x:%02x:%02x:%02x @ "
                  "%02x:%02x:%02x:%02x:%02x:%02x ch=%d rssi=%d\n",
                  a.vendor,
                  a.mac[0], a.mac[1], a.mac[2], a.mac[3], a.mac[4], a.mac[5],
                  a.bssid[0], a.bssid[1], a.bssid[2],
                  a.bssid[3], a.bssid[4], a.bssid[5],
                  (int)a.channel, (int)a.rssi);
    changed = true;
  }

  uint32_t now = millis();
  if (changed || now - lastUiUpdate > 400) {
    drawHeader();
    drawList();
    lastUiUpdate = now;
  }
  delay(10);
}

}  // namespace CameraScanner
