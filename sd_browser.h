/*
 * SD Browser — Hydra
 *
 * Read-only file/directory navigator for the SD card. Useful for confirming
 * that wardrive CSVs, handshake PCAPs, captured .sub files etc. actually
 * landed on disk.
 *
 * Controls:
 *   UP/DOWN — move cursor within current directory
 *   RIGHT   — enter directory under cursor / show file info
 *   LEFT    — go up one directory (refuses past `/`)
 *   SELECT  — exit
 *
 * Lists up to 64 entries per directory (paginated, 10 visible at a time).
 * No deletion / rename — those need a confirm UI and we can add later.
 */

#ifndef SD_BROWSER_H
#define SD_BROWSER_H

#include <Arduino.h>

namespace SdBrowser {

void sdBrowserSetup();
void sdBrowserLoop();

}  // namespace SdBrowser

#endif
