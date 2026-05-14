/*
 * target_list — shared AP target list for Hydra attacks
 *
 * The AP Scan & Select feature populates this list during a scan, and the
 * attack features (Targeted Deauth, SAE Commit, CSA, etc.) iterate over
 * entries with `selected == true`. Plain static array — no malloc, no
 * LinkedList, no dynamic resize. Survives across feature launches because
 * the array sits in BSS for the firmware's lifetime.
 */

#ifndef TARGET_LIST_H
#define TARGET_LIST_H

#include <Arduino.h>

namespace TargetList {

// 48 (was 64) — leaves DRAM for ESPPwn's per-target stats array + Hidden
// SSID's BSSID table + exclude list. Real-world scans rarely exceed 30
// unique APs; entries 49+ silently dropped by addOrUpdate().
#define HYDRA_MAX_TARGET_APS 48
#define HYDRA_SSID_MAX 33   // 32 byte SSID + null

enum AuthMode : uint8_t {
  AUTH_OPEN = 0,
  AUTH_WEP  = 1,
  AUTH_WPA  = 2,
  AUTH_WPA2 = 3,
  AUTH_WPA3 = 4
};

struct AccessPoint {
  uint8_t  bssid[6];
  char     ssid[HYDRA_SSID_MAX];
  int8_t   rssi;
  uint8_t  channel;
  uint8_t  auth;        // AuthMode
  bool     selected;
  uint32_t lastSeen;    // millis() when last beacon received
};

extern AccessPoint targets[HYDRA_MAX_TARGET_APS];
extern int targetCount;

// Wipe the list (set count to zero; doesn't memset the storage).
void clearTargets();

// Insert if not present (matched by BSSID), else update RSSI/channel/SSID.
// Returns index of the entry, or -1 if the list is full.
int addOrUpdate(const uint8_t* bssid,
                const char* ssid, uint8_t ssidLen,
                int8_t rssi,
                uint8_t channel,
                uint8_t auth);

// How many entries have selected == true.
int selectedCount();

// Toggle selection state at index. Returns new state, or false if out of range.
bool toggleSelected(int index);

}  // namespace TargetList

#endif
