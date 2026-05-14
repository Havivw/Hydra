/*
 * NRF24 Intermittent Jammer — Bruce-style PWM jammer
 *
 * Strobes the const carrier on/off at a user-adjustable duty cycle. More
 * effective than a pure carrier against protocols with framing detection
 * (the rising edges look like incoming frames). Also draws less average
 * current and runs cooler than the always-on carrier.
 */

#ifndef NRF_INTERMITTENT_H
#define NRF_INTERMITTENT_H

#include <Arduino.h>

namespace NrfIntermittent {

void intermittentSetup();
void intermittentLoop();

}  // namespace NrfIntermittent

#endif
