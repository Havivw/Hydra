/*
 * WPA3 SAE Commit Flood (lite) — Hydra
 *
 * Sends SAE Commit (Auth alg 3, seq 1, group 19 = NIST P-256) frames at each
 * selected AP. The scalar (32B) and element (64B) fields are filled with
 * cryptographically-random bytes via esp_fill_random — NOT a real ECP point.
 * APs reject these after the commit-validation step but still allocate
 * per-attempt session state during the time-of-check, which is enough to
 * stress WPA3-SAE auth handling.
 *
 * "Real" SAE commits would use mbedtls_ecp_mul to compute valid P-256 points
 * (Marauder's WiFiScan.cpp:6815-6882 does this). That's ~200 lines of ECP
 * setup and a bigger memory footprint; doable as a follow-up upgrade.
 *
 * Frame header lifted from marauder-v1div/esp32_marauder/WiFiScan.h:452
 * (sae_commit[32]).
 */

#ifndef SAE_ATTACK_H
#define SAE_ATTACK_H

#include <Arduino.h>

namespace SaeAttack {

void saeAttackSetup();
void saeAttackLoop();

}  // namespace SaeAttack

#endif
