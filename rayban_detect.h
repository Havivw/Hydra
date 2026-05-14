/*
 * Ray-Ban Meta Detector — Hydra port
 *
 * Detects Meta / Ray-Ban smart glasses BLE advertisements. Matches against
 * manufacturer/service IDs from Marauder's META_IDENTIFIERS table:
 *   0xFD5F, 0xFEB7, 0xFEB8, 0x01AB, 0x058E, 0x0D53 (Luxottica)
 *
 * Excludes anything matching BLOCKED_IDENTIFIERS (Samsung/Apple/MS/phone) so
 * a non-Meta device that happens to also publish a Meta-ish ID isn't reported.
 *
 * Source: marauder-v1div/esp32_marauder/WiFiScan.h:719 + WiFiScan.cpp:814-870.
 */

#ifndef RAYBAN_DETECT_H
#define RAYBAN_DETECT_H

#include <Arduino.h>

namespace RaybanDetect {

void raybanDetectSetup();
void raybanDetectLoop();

}  // namespace RaybanDetect

#endif
