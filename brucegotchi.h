/*
 * Brucegotchi — Pwnagotchi peer beacon spammer (the inverse of our
 * Pwnagotchi *detector*).
 *
 * Broadcasts 802.11 beacons carrying Pwnagotchi grid-spam JSON in vendor
 * IEs (tag 222 / 0xDE). Real pwnagotchis see us as a peer and update their
 * UI with our face/name. Cycles names+faces, hops channels 1/6/11.
 *
 * UP toggles run/stop. DOWN cycles between "regular" and "PWND" (DoS)
 * face/name pools — the PWND mode floods with all-block characters which
 * tends to wedge the target's screen on Pwnagotchi versions <1.7.
 */

#ifndef BRUCEGOTCHI_H
#define BRUCEGOTCHI_H

#include <Arduino.h>

namespace Brucegotchi {

void brucegotchiSetup();
void brucegotchiLoop();

}  // namespace Brucegotchi

#endif
