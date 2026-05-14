/*
 * Multi-SSID Detector — Hydra
 *
 * Flags BSSIDs (transmitter MACs) that have been observed broadcasting
 * more than 3 distinct SSIDs. That's the fingerprint of a portable hotspot
 * or rogue AP imitating multiple networks (e.g. WiFi Pineapple, Karma).
 *
 * Marauder reference: WIFI_SCAN_MULTISSID (constant 51).
 */

#ifndef MULTISSID_DETECT_H
#define MULTISSID_DETECT_H

#include <Arduino.h>

namespace MultiSsidDetect {

void multiSsidDetectSetup();
void multiSsidDetectLoop();

}  // namespace MultiSsidDetect

#endif
