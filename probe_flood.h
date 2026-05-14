/*
 * Probe Request Flood — Hydra
 *
 * Broadcasts a stream of 802.11 probe-request frames carrying random/random-ish
 * SSIDs. Effects:
 *   - Clutters the probe-request logs of every nearby AP / monitoring rig
 *   - Hides legitimate clients' probe-request fingerprints in noise
 *   - Disrupts PNL-based device-tracking and rogue-AP "karma"-style attacks
 *
 * Each frame has a random spoofed STA MAC (locally administered) and an SSID
 * pulled from a small pool plus random suffix. Hops across all 14 2.4GHz
 * channels round-robin so it touches every band.
 */

#ifndef PROBE_FLOOD_H
#define PROBE_FLOOD_H

#include <Arduino.h>

namespace ProbeFlood {

void probeFloodSetup();
void probeFloodLoop();

}  // namespace ProbeFlood

#endif
