/*
 * BLE Advertising Monitor (via NRF24) — Hydra
 *
 * The BLE advertising channels are at 2.402 / 2.426 / 2.480 GHz — which
 * map to NRF24 channels 2 / 26 / 80. By configuring the NRF24 with the
 * BLE access address 0x8E89BED6 as a pseudo-pipe-address (LSB-first), CRC
 * disabled, and 1 Mbps data rate, we can capture BLE adv packet preamble
 * matches and surface them.
 *
 * It's not a complete BLE decoder — the chip doesn't do whitening or full
 * CRC, so payloads need bit-reversal + de-whitening for full readability.
 * For a quick "what's around me" survey this is still very useful.
 *
 * UP/DOWN cycles the BLE adv channel (37/38/39). LEFT/RIGHT toggle hop.
 * SELECT exits.
 */

#ifndef NRF_BLE_ADV_H
#define NRF_BLE_ADV_H

#include <Arduino.h>

namespace NrfBleAdv {

void bleAdvSetup();
void bleAdvLoop();

}  // namespace NrfBleAdv

#endif
