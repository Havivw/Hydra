/*
 * Camera Scanner — Hydra
 *
 * Passive WiFi camera detector. Promiscuous monitor mode + channel hop;
 * for each 802.11 frame seen, checks the source/dest/BSSID addresses
 * against a curated list of known consumer- and prosumer-camera vendor
 * MAC OUIs. When a match is found, the camera's MAC, the BSSID it's
 * talking to, channel, RSSI, and (if AP Scan & Select was run earlier)
 * the human-readable SSID get surfaced on screen.
 *
 * For best results:
 *   1. Run WiFi -> AP Scan & Select first to populate the BSSID -> SSID
 *      lookup table.
 *   2. Then run Camera Scanner — the SSID column fills in for every camera
 *      that's associated with one of the scanned APs.
 *
 * If you skip step 1, the camera MAC + vendor still surface; the SSID
 * column shows "(unknown)".
 *
 * SELECT exits.
 */

#ifndef CAMERA_SCANNER_H
#define CAMERA_SCANNER_H

#include <Arduino.h>

namespace CameraScanner {

void cameraScannerSetup();
void cameraScannerLoop();

}  // namespace CameraScanner

#endif
