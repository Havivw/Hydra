/*
 * Evil Portal Detector — Hydra
 *
 * Heuristic detector for APs that look like captive-portal / evil twins:
 *   1. Beacon source MAC has the **open** auth bit (no privacy flag), AND
 *   2. SSID matches a common captive-portal name pattern
 *      (Free WiFi, Guest, Hotel, Airport, Cafe, etc.) — case-insensitive.
 *
 * No fixed Marauder source to lift verbatim; Marauder's WIFI_SCAN_EVIL_PORTAL
 * looks at SSID + behavior over time. This is a simpler heuristic that
 * surfaces likely candidates for manual investigation.
 */

#ifndef EVIL_PORTAL_DETECT_H
#define EVIL_PORTAL_DETECT_H

#include <Arduino.h>

namespace EvilPortalDetect {

void evilPortalDetectSetup();
void evilPortalDetectLoop();

}  // namespace EvilPortalDetect

#endif
