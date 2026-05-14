/*
 * Flipper Zero Detector — Hydra port
 *
 * Scans BLE advertisements for the Flipper Zero manufacturer ID 0x0FBA.
 * Reference: marauder-v1div/esp32_marauder/WiFiScan.cpp:246 — Flipper
 * advertising payload byte 0 = 0xBA (LSB), byte 1 = 0x0F (MSB).
 *
 * Uses BLEAdvertisedDeviceCallbacks for async detection — non-blocking,
 * so SELECT button stays responsive.
 */

#ifndef FLIPPER_DETECT_H
#define FLIPPER_DETECT_H

#include <Arduino.h>

namespace FlipperDetect {

void flipperDetectSetup();
void flipperDetectLoop();

}  // namespace FlipperDetect

#endif
