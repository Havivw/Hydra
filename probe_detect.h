/*
 * Probe Request Sniffer — Hydra port
 *
 * Captures 802.11 Probe Request frames (frame control 0x40) showing nearby
 * client devices searching for SSIDs they know. Dumps source MAC + the
 * requested ESSID. Useful for client-side reconnaissance.
 *
 * Source: marauder-v1div/esp32_marauder/WiFiScan.cpp:7066+ (probe-req branch
 * of the shared snifferCallback).
 */

#ifndef PROBE_DETECT_H
#define PROBE_DETECT_H

#include <Arduino.h>

namespace ProbeDetect {

void probeDetectSetup();
void probeDetectLoop();

}  // namespace ProbeDetect

#endif
