/*
 * Shared SubGHz constants and types — consolidates pin macros, EEPROM
 * profile layout, the cifertech default frequency list, and the RCSwitch
 * instance that were previously duplicated across the replayat,
 * SavedProfile, subjammer, SubReplay, SweepJammer, SubGhzWardrive,
 * FreqDetector, and SpectrumAnalyzer modules.
 *
 * Button pin defines (BTN_UP, BTN_DOWN, etc.) live in shared.h and are
 * included transitively.
 */

#ifndef SUB_SHARED_H
#define SUB_SHARED_H

#include <Arduino.h>
#include <RCSwitch.h>
#include "shared.h"

// CC1101 GDO data pins on DIV v1. GDO0 is wired to GPIO 26 (the line the
// RCSwitch driver calls TX_PIN); GDO2 to GPIO 16 (RX_PIN). CSN is GPIO 5
// — same physical pin as the SD card's CS — so every SubGHz feature that
// also touches SD has to mount one library before initialising the other.
// The ELECHOUSE library's ESP32 default of SS=5 is what we rely on; the
// firmware never calls setSpiPin() or setGDO() because the defaults happen
// to be correct for this hardware.
#define CC1101_GDO0_PIN 26
#define CC1101_GDO2_PIN 16

// EEPROM layout for stored Replay Attack signal + SavedProfile entries.
#define SUB_EEPROM_SIZE          1440
#define SUB_ADDR_VALUE           1280   // 4 bytes (uint32 — last received code)
#define SUB_ADDR_BITLEN          1284   // 2 bytes
#define SUB_ADDR_PROTO           1286   // 2 bytes
#define SUB_ADDR_FREQ            1288   // 4 bytes (selected freq index)
#define SUB_ADDR_PROFILE_COUNT   1296   // 4 bytes — was ADDR_PROFILE_START-4
#define SUB_ADDR_PROFILE_START   1300
#define SUB_MAX_PROFILES         5
#define SUB_MAX_NAME_LENGTH      16

struct SubProfile {
  uint32_t frequency;
  unsigned long value;
  int bitLength;
  int protocol;
  char name[SUB_MAX_NAME_LENGTH];
};

// 17-entry default-channel frequency list used by the cifertech-inherited
// replay / jammer features. Matches the entries marked "default" in
// wardrive_config.cpp::DEFAULT_INDEX_LIST. Owned here so the duplicated
// copies in replayat and subjammer namespaces can go away.
extern const uint32_t SUB_DEFAULT_FREQS[];
extern const int SUB_DEFAULT_FREQ_COUNT;

// One shared RCSwitch instance — used by Replay Attack for RX and by
// SavedProfile for TX. The previous setup declared two separate library
// objects on the same physical pins, which is at best wasteful and at
// worst a latent state-corruption bug.
extern RCSwitch subSwitch;

// Pin-claim handoff helper: every SubGHz feature must release GPIO 16/26
// from any prior NRF24 use before the CC1101 driver claims them, otherwise
// the radio driver fights with a still-active NRF24 instance. Calling this
// at the top of a feature setup() is cheap and idempotent.
inline void subghzReleasePinsFromNrf() {
  pinMode(CC1101_GDO0_PIN, INPUT);
  pinMode(CC1101_GDO2_PIN, INPUT);
}

#endif // SUB_SHARED_H
