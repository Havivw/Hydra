/*
 * Wardrive channel configuration — see header.
 */

#include "wardrive_config.h"
#include <string.h>

namespace WardriveConfig {

// 30-channel list covering the CC1101's three usable bands. Frequencies in
// Hz. The 17 channels marked here as "default" match cifertech's Replay
// Attack list — selectDefaults() reproduces that exact set.
FreqChannel channels[HYDRA_WARDRIVE_CHANNEL_COUNT] = {
  // Band 1: 300 - 348 MHz
  { 300000000, false },  // US legacy
  { 303875000, false },  // common keyfobs
  { 304250000, false },  // auto
  { 310000000, false },  // TPMS / wireless
  { 315000000, false },  // US ISM (garages, keyfobs)
  { 318000000, false },  // older garage doors
  { 345000000, false },  // some auto
  // Band 2: 387 - 464 MHz
  { 390000000, false },  // keyfobs
  { 418000000, false },  // older UK
  { 433075000, false },  // EU LPD low
  { 433420000, false },  // common keyfob
  { 433920000, false },  // PRIMARY EU ISM (most active)
  { 434420000, false },  // EU LPD
  { 434775000, false },  // EU LPD high
  { 438900000, false },  // weather stations
  { 446000000, false },  // PMR446 fringe
  { 459000000, false },  // some telemetry
  // Band 3: 779 - 928 MHz
  { 779000000, false },  // band low edge
  { 833000000, false },  // mid
  { 856000000, false },  // mid
  { 868000000, false },  // EU SRD bottom
  { 868350000, false },  // EU SRD primary
  { 868950000, false },  // EU SRD
  { 869500000, false },  // EU SRD high
  { 902000000, false },  // US ISM 900 bottom
  { 915000000, false },  // US ISM primary (LoRa/Z-Wave common)
  { 920000000, false },  // Asia
  { 925000000, false },  // EU/US upper
  { 927000000, false },  // upper
  { 928000000, false }   // band top edge
};

static bool initialized = false;

// The 17 cifertech defaults — match indices into the channels[] table above.
static const int DEFAULT_INDEX_LIST[] = {
  0,  // 300.000
  1,  // 303.875
  2,  // 304.250
  3,  // 310.000
  4,  // 315.000
  5,  // 318.000
  7,  // 390.000
  8,  // 418.000
  9,  // 433.075
  10, // 433.420
  11, // 433.920
  12, // 434.420
  13, // 434.775
  14, // 438.900
  21, // 868.350
  25, // 915.000
  28  // 925.000
};

void selectAll() {
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) channels[i].selected = true;
}

void selectNone() {
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) channels[i].selected = false;
}

void selectDefaults() {
  selectNone();
  for (size_t i = 0; i < sizeof(DEFAULT_INDEX_LIST) / sizeof(DEFAULT_INDEX_LIST[0]); i++) {
    int idx = DEFAULT_INDEX_LIST[i];
    if (idx >= 0 && idx < HYDRA_WARDRIVE_CHANNEL_COUNT) {
      channels[idx].selected = true;
    }
  }
}

void ensureInit() {
  if (initialized) return;
  selectDefaults();
  initialized = true;
}

int selectedCount() {
  ensureInit();
  int n = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) if (channels[i].selected) n++;
  return n;
}

bool toggleChannel(int idx) {
  ensureInit();
  if (idx < 0 || idx >= HYDRA_WARDRIVE_CHANNEL_COUNT) return false;
  channels[idx].selected = !channels[idx].selected;
  return channels[idx].selected;
}

}  // namespace WardriveConfig
