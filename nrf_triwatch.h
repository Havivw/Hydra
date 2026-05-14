/*
 * Tri-Radio Watch — Hydra (3-NRF24 simultaneous monitor)
 *
 * Unique to DIV v1's 3-radio hardware. The three NRF24L01 modules each
 * camp on a different user-pickable channel and surface activity in
 * parallel. Three live mini-channels at once = great for monitoring 3
 * adjacent WiFi/BLE bands or three suspected device frequencies.
 *
 * Pins:
 *   Radio 1: CE=16, CSN=17
 *   Radio 2: CE=26, CSN=27
 *   Radio 3: CE=4,  CSN=5
 *
 * Buttons:
 *   UP/DOWN   pick which radio slot is "selected"
 *   LEFT/RIGHT  channel down/up for the selected slot
 *   SELECT    exit
 */

#ifndef NRF_TRIWATCH_H
#define NRF_TRIWATCH_H

#include <Arduino.h>

namespace NrfTriWatch {

void triWatchSetup();
void triWatchLoop();

}  // namespace NrfTriWatch

#endif
