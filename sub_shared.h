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
#include <ELECHOUSE_CC1101_ESP32DIV.h>
#include "shared.h"

// CC1101 wiring on DIV v1, verified 2026-05-14 against the v1 shield
// schematic (`Previous versions/ESP32-DIV v1/Schematic/ESP32DIV-SHIELD.jpg`,
// component U1):
//   SCK  = GPIO 18 (VSPI SCK; shared with SD + NRF24 bus)
//   MISO = GPIO 19 (VSPI MISO; CC1101 chip pad is GDO1/SO multiplexed)
//   MOSI = GPIO 23 (VSPI MOSI)
//   CSN  = GPIO 27 (ALSO NRF24 #2 CSN — 2.4 GHz and sub-GHz mutually exclusive)
//   GDO0 = GPIO 26 (ALSO NRF24 #2 CE — RCSwitch driver calls this TX_PIN)
//   GDO2 = GPIO 16 (ALSO NRF24 #1 CE — RCSwitch driver calls this RX_PIN)
//
// The ELECHOUSE_CC1101 library's ESP32 default is SS=5, which is wrong
// for this board. Every CC1101 feature MUST call cc1101InitForDivV1()
// (defined below) instead of ELECHOUSE_cc1101.Init() directly. The bare
// Init() leaves CSN driven on GPIO 5 — where there's no chip — and the
// CC1101 silently ignores every SPI command, so setMHZ/setModulation/
// setRx/setSidle all fail without error.
#define CC1101_SCK_PIN  18
#define CC1101_MISO_PIN 19
#define CC1101_MOSI_PIN 23
#define CC1101_CSN_PIN  27
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

// Pin-claim handoff helper: every SubGHz feature must release GPIO 16/26/27
// from any prior NRF24 use before the CC1101 driver claims them, otherwise
// the radio driver fights with a still-active NRF24 instance. Calling this
// at the top of a feature setup() is cheap and idempotent.
inline void subghzReleasePinsFromNrf() {
  pinMode(CC1101_GDO0_PIN, INPUT);
  pinMode(CC1101_GDO2_PIN, INPUT);
  pinMode(CC1101_CSN_PIN,  INPUT);
}

// Initialise the CC1101 driver with the correct DIV v1 pin assignments.
// Replaces every bare ELECHOUSE_cc1101.Init() call site — calling the
// library's Init without first pinning SPI + GDO leaves CSN on GPIO 5
// (library default) where no chip exists.
inline void cc1101InitForDivV1() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK_PIN,
                             CC1101_MISO_PIN,
                             CC1101_MOSI_PIN,
                             CC1101_CSN_PIN);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0_PIN, CC1101_GDO2_PIN);
  ELECHOUSE_cc1101.Init();
}

#endif // SUB_SHARED_H
