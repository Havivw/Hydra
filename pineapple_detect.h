/*
 * WiFi Pineapple Detector — Hydra port
 *
 * Lifts the OUI + capability/tag heuristic from Marauder's
 * pineScanSnifferCallback / suspicious_vendors table (WiFiScan.cpp:6055+).
 *
 * Matching logic:
 *   1. Beacon source MAC's OUI is in the suspicious-vendors table AND its
 *      open/protected state matches the table's required flags, OR
 *   2. Capability info field == 0x0001 AND the only tagged parameter after
 *      the SSID IE is the DS Parameter Set (channel) — Pineapples emit
 *      minimal beacons matching this pattern.
 */

#ifndef PINEAPPLE_DETECT_H
#define PINEAPPLE_DETECT_H

#include <Arduino.h>

namespace PineappleDetect {

void pineappleDetectSetup();
void pineappleDetectLoop();

}  // namespace PineappleDetect

#endif
