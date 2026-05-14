/*
 * SD Format — Hydra
 *
 * Recursively deletes every file and directory on the SD card. The Arduino
 * ESP32 SD library doesn't expose a real FAT format() call, but for the
 * user-facing definition of "format my card" (wipe everything), a full
 * recursive purge has the same effect.
 *
 * UX is three screens: CONFIRM → PROGRESS → DONE. LEFT cancels at any point
 * (or SELECT to exit back to menu).
 */

#ifndef SD_FORMAT_H
#define SD_FORMAT_H

#include <Arduino.h>

namespace SdFormat {

void sdFormatSetup();
void sdFormatLoop();

}  // namespace SdFormat

#endif
