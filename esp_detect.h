/*
 * Espressif Detector — Hydra port
 *
 * Flags 802.11 beacons sourced from known Espressif chip OUIs (ESP32 /
 * ESP8266 / etc.). Marauder ships a single-OUI table at
 * marauder-v1div/esp32_marauder/Assets.h:10 (PROGMEM espressif_macs[] = { "fc:f5:c4" }).
 *
 * Expanded here with a few more well-known Espressif allocations.
 */

#ifndef ESP_DETECT_H
#define ESP_DETECT_H

#include <Arduino.h>

namespace EspDetect {

void espDetectSetup();
void espDetectLoop();

}  // namespace EspDetect

#endif
