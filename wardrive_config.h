/*
 * Wardrive channel configuration — Hydra
 *
 * Shared list of every sub-GHz channel the wardrive feature can sweep.
 * Settings → Wardrive Channels lets the user toggle which ones are active.
 * Selections live in RAM and persist across feature launches.
 *
 * Three frequency bands matching the CC1101's hardware ranges:
 *   300 - 348 MHz
 *   387 - 464 MHz
 *   779 - 928 MHz
 *
 * The 17 channels cifertech's Replay Attack uses are pre-selected on
 * init so the wardrive default behaviour is unchanged.
 */

#ifndef WARDRIVE_CONFIG_H
#define WARDRIVE_CONFIG_H

#include <Arduino.h>

namespace WardriveConfig {

#define HYDRA_WARDRIVE_CHANNEL_COUNT 30

struct FreqChannel {
  uint32_t hz;
  bool     selected;
};

extern FreqChannel channels[HYDRA_WARDRIVE_CHANNEL_COUNT];

// Lazy init — call before reading. Idempotent.
void ensureInit();

// How many channels currently have selected == true.
int selectedCount();

// Flip selection on the channel at this index. Returns the new state, or
// false if idx is out of range. Out-of-range returns false (no-op).
bool toggleChannel(int idx);

// Select / deselect everything (handy for the settings UI).
void selectAll();
void selectNone();
void selectDefaults();

}  // namespace WardriveConfig

#endif
