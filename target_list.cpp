/*
 * target_list — see header.
 */

#include "target_list.h"
#include <string.h>

namespace TargetList {

AccessPoint targets[HYDRA_MAX_TARGET_APS];
int targetCount = 0;

void clearTargets() {
  targetCount = 0;
}

// True if the SSID buffer has at least one printable byte. Some APs hide their
// network by broadcasting beacons with ssidLen > 0 but all bytes set to 0x00
// — these also count as hidden, not as "[hidden_with_zeros_named_'????']".
static bool hasPrintableSsid(const char* ssid, uint8_t ssidLen) {
  for (int k = 0; k < ssidLen; k++) {
    if (ssid[k] >= 32 && ssid[k] < 127) return true;
  }
  return false;
}

int addOrUpdate(const uint8_t* bssid,
                const char* ssid, uint8_t ssidLen,
                int8_t rssi,
                uint8_t channel,
                uint8_t auth) {
  bool realSsid = hasPrintableSsid(ssid, ssidLen);

  // Update existing
  for (int i = 0; i < targetCount; i++) {
    if (memcmp(targets[i].bssid, bssid, 6) == 0) {
      if (rssi > targets[i].rssi) targets[i].rssi = rssi;
      targets[i].channel = channel;
      // Only overwrite SSID if the new frame carries a real (printable) name.
      // This lets a Probe Response (0x50) upgrade an entry from "[hidden]"
      // to the real network name, without a subsequent zero-SSID beacon
      // wiping it back to "[hidden]".
      if (realSsid) {
        int n = ssidLen;
        if (n > HYDRA_SSID_MAX - 1) n = HYDRA_SSID_MAX - 1;
        for (int k = 0; k < n; k++) {
          char c = ssid[k];
          targets[i].ssid[k] = (c >= 32 && c < 127) ? c : '?';
        }
        targets[i].ssid[n] = '\0';
      }
      targets[i].auth = auth;
      targets[i].lastSeen = millis();
      return i;
    }
  }

  if (targetCount >= HYDRA_MAX_TARGET_APS) return -1;

  AccessPoint& a = targets[targetCount];
  memcpy(a.bssid, bssid, 6);
  if (realSsid) {
    int n = ssidLen;
    if (n > HYDRA_SSID_MAX - 1) n = HYDRA_SSID_MAX - 1;
    for (int k = 0; k < n; k++) {
      char c = ssid[k];
      a.ssid[k] = (c >= 32 && c < 127) ? c : '?';
    }
    a.ssid[n] = '\0';
  } else {
    strncpy(a.ssid, "[hidden]", HYDRA_SSID_MAX);
  }
  a.rssi = rssi;
  a.channel = channel;
  a.auth = auth;
  a.selected = false;
  a.lastSeen = millis();
  return targetCount++;
}

int selectedCount() {
  int n = 0;
  for (int i = 0; i < targetCount; i++) if (targets[i].selected) n++;
  return n;
}

bool toggleSelected(int index) {
  if (index < 0 || index >= targetCount) return false;
  targets[index].selected = !targets[index].selected;
  return targets[index].selected;
}

}  // namespace TargetList
