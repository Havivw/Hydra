/*
 * Targeted Deauth — Hydra
 *
 * For each AP marked `selected` in TargetList::targets[], hops to that AP's
 * channel and TX-broadcasts a deauth frame spoofing the AP as the source.
 * Effectively kicks every client off the selected networks.
 *
 * Frame template lifted from marauder-v1div/esp32_marauder/WiFiScan.h:491
 * (deauth_frame_default[26]) and the sendDeauthFrame logic at WiFiScan.cpp:7825.
 *
 * UP toggles the attack on/off so the user can preview targets, prep, then
 * start. SELECT exits to the WiFi submenu.
 */

#ifndef DEAUTH_ATTACK_H
#define DEAUTH_ATTACK_H

#include <Arduino.h>

namespace DeauthAttack {

void deauthAttackSetup();
void deauthAttackLoop();

}  // namespace DeauthAttack

#endif
