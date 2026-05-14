/*
 * AirTag Detector — Hydra port
 *
 * Scans BLE for Apple Find My / AirTag advertisements. Signature lifted from
 * marauder-v1div/esp32_marauder/WiFiScan.cpp:331-345:
 *   payload contains the byte sequence 0x1E 0xFF 0x4C 0x00  (Apple manuf 0x004C, len 0x1E)
 *   OR the sequence 0x4C 0x00 0x12 0x19  (Apple + Find My type 0x12, len 0x19).
 */

#ifndef AIRTAG_DETECT_H
#define AIRTAG_DETECT_H

#include <Arduino.h>

namespace AirTagDetect {

void airtagDetectSetup();
void airtagDetectLoop();

}  // namespace AirTagDetect

#endif
