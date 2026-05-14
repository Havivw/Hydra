/*
 * NRF24 Capture + Replay — Hydra
 *
 * Two-phase tool. In CAPTURE state, listens promiscuously on a user-pickable
 * channel and stops as soon as the first plausible 32-byte packet arrives.
 * In REPLAY state, that captured packet is transmitted on the same channel
 * (repeatedly while UP is held, or in a manual burst).
 *
 * NRF24 analog to cifertech's sub-GHz Replay Attack — useful for analysing
 * simple 2.4 GHz toys / car remotes / DIY devices that use NRF24-style
 * unencrypted protocols.
 *
 * Buttons:
 *   LEFT/RIGHT   channel down/up
 *   UP           start CAPTURE (waits for one packet) → arms REPLAY
 *                if a packet is already captured, REPLAYS it
 *   DOWN         clear captured packet (back to "empty")
 *   SELECT       exit
 */

#ifndef NRF_REPLAY_H
#define NRF_REPLAY_H

#include <Arduino.h>

namespace NrfReplay {

void replaySetup();
void replayLoop();

}  // namespace NrfReplay

#endif
