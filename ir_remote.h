/*
 * IR Remote — Hydra
 *
 * Three modal entry points, one per main-menu IR-submenu item:
 *   irRecvSetup/Loop    — live IR receiver. Decodes via IRremoteESP8266
 *                          and shows protocol + value. Last decode is
 *                          retained as the global capture for Replay.
 *   irReplaySetup/Loop  — replays the most recent capture. UP = fire once
 *                          or toggle auto-fire. DOWN = stop.
 *   irTvOffSetup/Loop   — TV-B-Gone Lite. Walks ~30 curated power-off
 *                          codes (NEC/Sony/Samsung/LG/Panasonic/RC5/RC6).
 *                          UP toggles sweep, DOWN advances code manually.
 *
 * Hardware pins (from upstream shared.h):
 *   IR_RX = GPIO 21, IR_TX = GPIO 14, default carrier 38 kHz.
 *
 * On the v1 board these collide with optional NRF24 #3; the v1 PCB wires
 * NRF3 at GPIO 4/5, so 21/14 are free for IR.
 *
 * The captured frame is persisted across mode switches — capture once in
 * RECV, then enter REPLAY to send it.
 */

#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <Arduino.h>

namespace IrRemote {

void irRecvSetup();
void irRecvLoop();

void irReplaySetup();
void irReplayLoop();

void irTvOffSetup();
void irTvOffLoop();

}  // namespace IrRemote

#endif
