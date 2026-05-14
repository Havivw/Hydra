/*
 * NRF24 2.4 GHz Wardrive — Hydra
 *
 * Companion to SubGHz Wardrive. Sweeps NRF24 channels 0..125 (2400-2525 MHz),
 * carrier-detects activity, and logs each new hit to SD with the current
 * GPS fix:
 *
 *   utc_time,date,latitude,longitude,channel,freq_mhz,sats
 *
 * Hits are rate-limited per channel so an always-on transmitter doesn't
 * spam the CSV. File at /hydra/nrf_wardrive.csv.
 *
 * SELECT exits. UP toggles auto-hop on/off (stay on current channel).
 */

#ifndef NRF_WARDRIVE_H
#define NRF_WARDRIVE_H

#include <Arduino.h>

namespace NrfWardrive {

void nrfWardriveSetup();
void nrfWardriveLoop();

}  // namespace NrfWardrive

#endif
