/*
 * AP Scan & Select — see header.
 */

#include "ap_scan.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

namespace ApScan {

enum Phase {
  PHASE_SCAN   = 0,
  PHASE_SELECT = 1
};

static Phase   phase = PHASE_SCAN;
static uint32_t phaseStartedAt = 0;
static const uint32_t SCAN_DURATION_MS = 12000;

// ============================================================
// SCAN PHASE — sniffer callback parses beacons into TargetList
// ============================================================
static const uint32_t HOP_DWELL_MS = 350;
static uint8_t currentChannel = 1;
static uint32_t lastHop = 0;
static uint32_t lastTopRedraw = 0;

// Map IEEE 802.11 RSN/WPA elements into a coarse auth bucket.
// Beacon capability bit 4 (privacy) tells us if it's encrypted at all.
static uint8_t classifyAuth(const uint8_t* payload, int len, uint16_t capab) {
  bool privacy = (capab & 0x10) != 0;
  if (!privacy) return TargetList::AUTH_OPEN;

  if (len < 38) return TargetList::AUTH_WEP;
  int pos = 36 + payload[37] + 2;  // skip fixed header + SSID IE
  bool sawRSN = false;
  bool sawWPA = false;
  bool sawSAE = false;
  while (pos + 2 < len) {
    uint8_t tag = payload[pos];
    uint8_t tlen = payload[pos + 1];
    if (pos + 2 + tlen > len) break;
    if (tag == 0x30) {  // RSN
      sawRSN = true;
      // AKM list starts at offset 12 within the RSN element. Each AKM is
      // 4 bytes; suite type 8 = SAE (WPA3). Scan up to a few suites.
      const uint8_t* rsn = payload + pos + 2;
      if (tlen >= 14) {
        uint16_t akmCount = (uint16_t)rsn[12] | ((uint16_t)rsn[13] << 8);
        for (int k = 0; k < akmCount && (14 + 4 * (k + 1)) <= tlen; k++) {
          uint8_t akmType = rsn[14 + 4 * k + 3];
          if (akmType == 8) sawSAE = true;
        }
      }
    } else if (tag == 0xdd && tlen >= 4) {
      // Vendor specific — Microsoft OUI 00:50:F2 type 1 = WPA1
      if (payload[pos + 2] == 0x00 && payload[pos + 3] == 0x50 &&
          payload[pos + 4] == 0xF2 && payload[pos + 5] == 0x01) {
        sawWPA = true;
      }
    }
    pos += tlen + 2;
  }
  if (sawSAE) return TargetList::AUTH_WPA3;
  if (sawRSN) return TargetList::AUTH_WPA2;
  if (sawWPA) return TargetList::AUTH_WPA;
  return TargetList::AUTH_WEP;
}

static void IRAM_ATTR wifiSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 38) return;
  // Beacon (0x80) OR Probe Response (0x50). Both have identical layout:
  // 24-byte header + 12-byte fixed body (timestamp/interval/capability) +
  // variable IEs starting at byte 36, SSID IE first. Probe responses are
  // emitted by hidden APs when a client probes them directly by name —
  // catching them lets us upgrade a "[hidden]" row to the real SSID.
  if (payload[0] != 0x80 && payload[0] != 0x50) return;

  uint16_t capab = (uint16_t)payload[34] | ((uint16_t)payload[35] << 8);
  uint8_t auth = classifyAuth(payload, len, capab);

  uint8_t ssidLen = payload[37];
  if (ssidLen > 32) ssidLen = 32;
  const char* ssid = (const char*)(payload + 38);
  if (38 + ssidLen > len) return;

  // The promiscuous callback may run before tasks resolve writes back to
  // BSS — TargetList::addOrUpdate is non-IRAM but safe enough at this
  // packet rate. Calling from IRAM only matters when it returns fast.
  TargetList::addOrUpdate(payload + 10, ssid, ssidLen,
                          pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, auth);
}

static void doChannelHop() {
  uint32_t now = millis();
  if (now - lastHop < HOP_DWELL_MS) return;
  currentChannel = (currentChannel % 11) + 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = now;
}

// ============================================================
// SELECT PHASE — list UI
// ============================================================
static int selectCursor = 0;
static int selectViewOffset = 0;
#define SELECT_VISIBLE_ROWS 13
#define SELECT_ROW_HEIGHT 18
#define SELECT_TOP_Y 60
static uint32_t lastNavMs = 0;
static const uint32_t NAV_DEBOUNCE_MS = 180;

// Long-press handling for LEFT/RIGHT: short tap toggles the cursor row,
// long hold (≥LONG_PRESS_MS) clears or selects ALL targets.
//
// PCF8574 reads jitter for ~10-20 ms after a press, and a single momentary
// `false` reading was canceling the in-progress long-press. We track:
//   pressedSince — first instant we saw the button down
//   lastSeenDown — most recent instant the button read down
// and only treat the button as "released" when it's been reliably off
// for ≥BOUNCE_MS. That makes the press-time monotonic across bounces.
static uint32_t leftPressedSince  = 0;
static uint32_t leftLastSeenDown  = 0;
static uint32_t rightPressedSince = 0;
static uint32_t rightLastSeenDown = 0;
static bool     leftLongFired     = false;
static bool     rightLongFired    = false;
static const uint32_t LONG_PRESS_MS = 500;
static const uint32_t BOUNCE_MS     = 40;

static void selectAllTargets() {
  for (int i = 0; i < TargetList::targetCount; i++) {
    TargetList::targets[i].selected = true;
  }
}
static void clearAllSelected() {
  for (int i = 0; i < TargetList::targetCount; i++) {
    TargetList::targets[i].selected = false;
  }
}

static const char* authShort(uint8_t a) {
  switch (a) {
    case TargetList::AUTH_OPEN: return "OPEN";
    case TargetList::AUTH_WEP:  return "WEP ";
    case TargetList::AUTH_WPA:  return "WPA ";
    case TargetList::AUTH_WPA2: return "WPA2";
    case TargetList::AUTH_WPA3: return "WPA3";
    default:                    return "?   ";
  }
}

static void drawSelectRow(int slot, int idx) {
  int y = SELECT_TOP_Y + slot * SELECT_ROW_HEIGHT;
  tft.fillRect(0, y, tft.width(), SELECT_ROW_HEIGHT, TFT_BLACK);

  bool highlighted = (idx == selectCursor);
  uint16_t fg = highlighted ? TFT_ORANGE : TFT_WHITE;
  tft.setTextColor(fg, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setCursor(2, y + 4);
  const TargetList::AccessPoint& a = TargetList::targets[idx];
  // [X] vs [ ] selection mark
  tft.print(a.selected ? "[X] " : "[ ] ");
  // SSID truncated to 16 chars to leave room for meta
  char ssid_show[17];
  int sl = strlen(a.ssid);
  if (sl > 16) sl = 16;
  memcpy(ssid_show, a.ssid, sl);
  ssid_show[sl] = '\0';
  tft.print(ssid_show);

  // Right-aligned meta: auth, channel, RSSI
  tft.setCursor(170, y + 4);
  tft.printf("%s c%-2d %d", authShort(a.auth), (int)a.channel, (int)a.rssi);
}

static void drawSelectFull() {
  tft.fillRect(0, SELECT_TOP_Y, tft.width(),
               SELECT_VISIBLE_ROWS * SELECT_ROW_HEIGHT, TFT_BLACK);
  int n = TargetList::targetCount - selectViewOffset;
  if (n > SELECT_VISIBLE_ROWS) n = SELECT_VISIBLE_ROWS;
  for (int s = 0; s < n; s++) drawSelectRow(s, selectViewOffset + s);

  // Scroll chevrons
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (selectViewOffset > 0) {
    tft.setCursor(228, SELECT_TOP_Y);
    tft.print("^");
  }
  if (selectViewOffset + n < TargetList::targetCount) {
    tft.setCursor(228, SELECT_TOP_Y + (n - 1) * SELECT_ROW_HEIGHT);
    tft.print("v");
  }
}

static void ensureCursorVisible() {
  if (selectCursor < selectViewOffset) {
    selectViewOffset = selectCursor;
  } else if (selectCursor >= selectViewOffset + SELECT_VISIBLE_ROWS) {
    selectViewOffset = selectCursor - SELECT_VISIBLE_ROWS + 1;
  }
  int maxOff = TargetList::targetCount - SELECT_VISIBLE_ROWS;
  if (maxOff < 0) maxOff = 0;
  if (selectViewOffset > maxOff) selectViewOffset = maxOff;
  if (selectViewOffset < 0) selectViewOffset = 0;
}

static void redrawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  if (phase == PHASE_SCAN) {
    uint32_t elapsed = millis() - phaseStartedAt;
    uint32_t left = (SCAN_DURATION_MS > elapsed) ? (SCAN_DURATION_MS - elapsed) : 0;
    tft.printf("SCAN ch%d  %ds  found:%d",
               (int)currentChannel, (int)(left / 1000) + 1, TargetList::targetCount);
  } else {
    tft.printf("SEL %d/%d  L/R tap=tog hold=All",
               TargetList::selectedCount(), TargetList::targetCount);
  }
}

// ============================================================
// PHASE TRANSITION
// ============================================================
static void enterSelectPhase() {
  phase = PHASE_SELECT;
  selectCursor = 0;
  selectViewOffset = 0;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  // Show header bar instructions + the list
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.printf("Found %d APs. L/R tap=tog hold=all", TargetList::targetCount);

  drawSelectFull();
  redrawTopStatus();
  Serial.printf("[ApScan] scan complete, found %d APs\n", TargetList::targetCount);
}

// ============================================================
// PUBLIC API
// ============================================================
void apScanSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  TargetList::clearTargets();
  phase = PHASE_SCAN;
  phaseStartedAt = millis();

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.print("[!] AP Scan & Select");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  // Defensive reset — previous features (ESPPwnagotchi, EvilPortal+Deauth,
  // Brucegotchi, etc.) leave WiFi in AP mode with a Data-only promisc filter
  // and their own RX callback. We have to undo all of that or the sniffer
  // here never sees beacons. Order matters: stop promisc -> change mode ->
  // restart -> install our filter+cb -> enable promisc.
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&wifiSniffer);
  esp_wifi_set_promiscuous(true);
  currentChannel = 1;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  lastHop = millis();

  Serial.println("[ApScan] sniffer started, 12 sec scan over channels 1..11");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  redrawTopStatus();
  lastTopRedraw = millis();
}

void apScanLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  if (phase == PHASE_SCAN) {
    doChannelHop();
    uint32_t now = millis();
    if (now - phaseStartedAt >= SCAN_DURATION_MS) {
      enterSelectPhase();
      return;
    }
    if (now - lastTopRedraw > 200) {
      redrawTopStatus();
      lastTopRedraw = now;
    }
    delay(10);
    return;
  }

  // PHASE_SELECT
  uint32_t now = millis();
  bool needRedraw = false;

  bool leftDown  = !pcf.digitalRead(BTN_LEFT);
  bool rightDown = !pcf.digitalRead(BTN_RIGHT);

  // ---- LEFT ----
  if (leftDown) {
    leftLastSeenDown = now;
    if (leftPressedSince == 0) leftPressedSince = now;
    if (!leftLongFired && now - leftPressedSince >= LONG_PRESS_MS) {
      clearAllSelected();
      leftLongFired = true;
      needRedraw = true;
      Serial.printf("[ApScan] LONG-LEFT after %ums → cleared ALL (%d targets)\n",
                    (unsigned)(now - leftPressedSince), TargetList::targetCount);
    }
  } else if (leftPressedSince > 0 && now - leftLastSeenDown >= BOUNCE_MS) {
    // Truly released (off for BOUNCE_MS straight).
    if (!leftLongFired && TargetList::targetCount > 0) {
      TargetList::toggleSelected(selectCursor);
      needRedraw = true;
      Serial.printf("[ApScan] short-LEFT (%ums) → toggle idx %d\n",
                    (unsigned)(leftLastSeenDown - leftPressedSince), selectCursor);
    }
    leftPressedSince = 0;
    leftLastSeenDown = 0;
    leftLongFired = false;
  }

  // ---- RIGHT ----
  if (rightDown) {
    rightLastSeenDown = now;
    if (rightPressedSince == 0) rightPressedSince = now;
    if (!rightLongFired && now - rightPressedSince >= LONG_PRESS_MS) {
      selectAllTargets();
      rightLongFired = true;
      needRedraw = true;
      Serial.printf("[ApScan] LONG-RIGHT after %ums → selected ALL (%d targets)\n",
                    (unsigned)(now - rightPressedSince), TargetList::targetCount);
    }
  } else if (rightPressedSince > 0 && now - rightLastSeenDown >= BOUNCE_MS) {
    if (!rightLongFired && TargetList::targetCount > 0) {
      TargetList::toggleSelected(selectCursor);
      needRedraw = true;
      Serial.printf("[ApScan] short-RIGHT (%ums) → toggle idx %d\n",
                    (unsigned)(rightLastSeenDown - rightPressedSince), selectCursor);
    }
    rightPressedSince = 0;
    rightLastSeenDown = 0;
    rightLongFired = false;
  }

  // UP/DOWN: navigate cursor (debounced so a held button doesn't fly past).
  if (now - lastNavMs > NAV_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_DOWN) && TargetList::targetCount > 0) {
      selectCursor = (selectCursor + 1) % TargetList::targetCount;
      lastNavMs = now;
      needRedraw = true;
    } else if (!pcf.digitalRead(BTN_UP) && TargetList::targetCount > 0) {
      selectCursor--;
      if (selectCursor < 0) selectCursor = TargetList::targetCount - 1;
      lastNavMs = now;
      needRedraw = true;
    }
  }

  if (needRedraw) {
    ensureCursorVisible();
    drawSelectFull();
    redrawTopStatus();
  } else if (now - lastTopRedraw > 500) {
    redrawTopStatus();
    lastTopRedraw = now;
  }

  delay(10);
}

}  // namespace ApScan
