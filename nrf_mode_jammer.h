/*
 * NRF Mode Jammer — Hydra
 *
 * Profile-based 2.4 GHz jammer: hops the NRF24 const carrier across a
 * channel list selected by the current profile.
 *
 *   BLE-ADV   — hops 2/26/80 (= BLE advertising channels 37/38/39).
 *               Equivalent to BlueJammer's BLE mode.
 *   BT        — full 2-80 hop with stride 2. Bluetooth Classic uses 79
 *               channels in 2402-2480 MHz; we cover all of them.
 *   WiFi      — hops the 2.4 GHz WiFi 1/6/11 envelopes (NRF channel
 *               numbers 0..22, 25..47, 50..72).
 *   FULL      — full 0..125 sweep (covers everything in 2.4 GHz).
 *
 * Single NRF1 only — NRF2/NRF3 share pins with CC1101/SD/TFT backlight on
 * this board and are unreliable. RF-Clown's triple-radio approach can't
 * map cleanly here, but a single radio hopping fast across the same total
 * channel set has equivalent broad-spectrum effect.
 */

#ifndef NRF_MODE_JAMMER_H
#define NRF_MODE_JAMMER_H

#include <Arduino.h>

namespace NrfModeJammer {

void modeJammerSetup();
void modeJammerLoop();

}  // namespace NrfModeJammer

#endif
