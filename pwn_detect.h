/*
 * Pwnagotchi Detector — Hydra port
 *
 * Sniffs 2.4 GHz for beacon frames sourced from MAC de:ad:be:ef:de:ad.
 * Pwnagotchi devices broadcast their state as a JSON payload embedded in
 * the beacon (vendor IE area). The name field is extracted for display.
 *
 * Ported from marauder-v1div/esp32_marauder/WiFiScan.cpp:6001-6053
 * (processPwnagotchiBeacon) and the WIFI_SCAN_PWN code path. No ArduinoJson
 * dep — we hand-parse "name" as a substring.
 */

#ifndef PWN_DETECT_H
#define PWN_DETECT_H

#include <Arduino.h>

namespace PwnDetect {

void pwnDetectSetup();
void pwnDetectLoop();

}  // namespace PwnDetect

#endif
