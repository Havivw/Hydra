/*
 * CSA Broadcast Attack — Hydra
 *
 * For each selected AP, broadcasts spoofed beacon frames carrying a
 * Channel-Switch Announcement (CSA) IE that tells clients to switch to a
 * different (bogus) channel. Compliant clients follow the announcement and
 * lose their connection.
 *
 * Frame builder modelled on marauder-v1div/esp32_marauder/WiFiScan.cpp:7447
 * (broadcastCustomBeacon with scan_mode == WIFI_ATTACK_CSA). The CSA IE is
 * tag 37 (0x25), length 3, body: mode | new_channel | count.
 */

#ifndef CSA_ATTACK_H
#define CSA_ATTACK_H

#include <Arduino.h>

namespace CsaAttack {

void csaAttackSetup();
void csaAttackLoop();

}  // namespace CsaAttack

#endif
