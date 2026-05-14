/*
 * SubGHz Wardrive — Hydra
 *
 * CC1101 sweeps the 17 sub-GHz channels cifertech already uses (300 MHz to
 * 925 MHz). When RSSI on the current channel crosses a threshold, the
 * detection is timestamped, GPS-tagged from the Gps:: subsystem, and
 * appended as a row to /hydra/sub_wardrive.csv on the SD card.
 *
 * Each detection row:
 *   utc_time,latitude,longitude,freq_hz,rssi_dbm,sats
 *
 * Lifts the CC1101 dwell+SetRx pattern from cifertech's subghz.cpp Replay
 * Attack. Reuses the same subghz_frequency_list from there.
 *
 * UP toggles auto-hop (otherwise stays on the current freq). SELECT exits.
 */

#ifndef SUBGHZ_WARDRIVE_H
#define SUBGHZ_WARDRIVE_H

#include <Arduino.h>

namespace SubGhzWardrive {

void wardriveSetup();
void wardriveLoop();

}  // namespace SubGhzWardrive

#endif
