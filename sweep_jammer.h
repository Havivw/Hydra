/*
 * CC1101 Frequency Sweep Jammer — Hydra
 *
 * Fast continuous-carrier sweep across the Wardrive-enabled channels.
 * Different from the existing SubGHz Jammer (which dwells on a fixed
 * frequency-list and only sweeps slowly in auto mode):
 *
 *   - Tiny per-channel dwell (~3 ms) — broadband effect
 *   - Walks WardriveConfig::channels[] in order, looping forever
 *   - User toggles ON/OFF, can pick "selected channels only" vs "all"
 *
 * Useful against frequency-hopping protocols (some garage doors, alarm
 * fobs, basic LoRa). On stationary single-freq targets, the dedicated
 * SubGHz Jammer is more effective.
 */

#ifndef SWEEP_JAMMER_H
#define SWEEP_JAMMER_H

#include <Arduino.h>

namespace SweepJammer {

void sweepJammerSetup();
void sweepJammerLoop();

}  // namespace SweepJammer

#endif
