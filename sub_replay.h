/*
 * Flipper .sub file replay — Hydra (SubMarine Phase 3)
 *
 * Browse /hydra/sub_files/*.sub on the SD card, pick one, transmit it via
 * the CC1101 in OOK async mode. The replay path bit-bangs GDO0 (CC1101
 * TX_PIN on DIV v1 = GPIO 26), matching SubMarine / Flipper Zero raw OOK.
 *
 * Supported file properties (per the Flipper RAW format):
 *   Filetype: Flipper SubGhz RAW File
 *   Frequency: <hz>
 *   Preset: FuriHalSubGhzPresetOok650Async | FuriHalSubGhzPresetOok270Async
 *   Protocol: RAW
 *   RAW_Data: <signed µs durations>
 *
 * Other presets (2-FSK / GFSK / MSK) reject with an "unsupported preset"
 * message — the OOK presets cover most public Flipper signal libraries.
 *
 * Buttons:
 *   LIST   → UP/DOWN nav, RIGHT = pick file
 *   INFO   → UP = transmit, LEFT = back to list
 *   TX     → auto-completes, SELECT aborts
 *   DONE   → any key = back to list
 *   SELECT exits the feature from LIST or DONE.
 */

#ifndef SUB_REPLAY_H
#define SUB_REPLAY_H

#include <Arduino.h>

namespace SubReplay {

void subReplaySetup();
void subReplayLoop();

}  // namespace SubReplay

#endif
