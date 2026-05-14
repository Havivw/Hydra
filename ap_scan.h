/*
 * AP Scan & Select — Hydra
 *
 * Two-phase feature:
 *   PHASE_SCAN  (~12 sec): WiFi promiscuous mode, channel-hop 1..11, sniff
 *               beacons, populate TargetList.
 *   PHASE_SELECT: scrollable list of scanned APs. UP/DOWN navigates;
 *               LEFT or RIGHT toggles `selected` on the current AP. SELECT
 *               exits back to the menu — selection state persists in the
 *               global TargetList array for attack features to read.
 */

#ifndef AP_SCAN_H
#define AP_SCAN_H

#include <Arduino.h>

namespace ApScan {

void apScanSetup();
void apScanLoop();

}  // namespace ApScan

#endif
