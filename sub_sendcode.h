/*
 * SubGHz manual code transmitter
 *
 * Pick an rc-switch protocol (1..12), a frequency, a bit length, and an
 * arbitrary code value — and transmit. Useful for sending known fixed-
 * code signals without first having to capture them (e.g. a code you
 * read off a remote's PCB DIP switches, or one you decoded on a
 * different device).
 *
 * Five phases: PROTO → FREQ → CODE → BITLEN → READY (review + TX).
 * SELECT exits at any phase. From READY, UP transmits, LEFT goes back
 * a step.
 */

#ifndef HYDRA_SUB_SENDCODE_H
#define HYDRA_SUB_SENDCODE_H

#include <Arduino.h>

namespace SubSendCode {

void subSendCodeSetup();
void subSendCodeLoop();

}  // namespace SubSendCode

#endif
