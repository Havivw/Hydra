/*
 * NRF24 Constant Carrier Jammer — Hydra
 *
 * Continuously transmits a constant carrier on one user-selected NRF24
 * channel. Narrow, targeted denial. Different from cifertech's Proto Kill
 * which iterates a protocol's channel group; this stays on exactly the
 * channel you point it at.
 *
 * Buttons:
 *   LEFT  / RIGHT  channel down / up
 *   UP             toggle TX on/off
 *   SELECT         exit (auto-disables TX on exit)
 */

#ifndef NRF_CARRIER_H
#define NRF_CARRIER_H

#include <Arduino.h>

namespace NrfCarrier {

void carrierSetup();
void carrierLoop();

}  // namespace NrfCarrier

#endif
