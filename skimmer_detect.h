/*
 * Skimmer Detector — Hydra port
 *
 * Flags BLE devices broadcasting names common to cheap Bluetooth modules
 * used in credit card skimmers: HC-03, HC-05, HC-06.
 *
 * From marauder-v1div/esp32_marauder/WiFiScan.cpp:768
 *   String bad_list[] = {"HC-03", "HC-05", "HC-06"};
 */

#ifndef SKIMMER_DETECT_H
#define SKIMMER_DETECT_H

#include <Arduino.h>

namespace SkimmerDetect {

void skimmerDetectSetup();
void skimmerDetectLoop();

}  // namespace SkimmerDetect

#endif
