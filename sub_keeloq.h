/*
 * Manual KeeLoq transmitter
 *
 * Walks the user through (mfr → frequency → serial → button → counter)
 * and transmits a fresh KeeLoq frame using the encode primitives in
 * keeloq_common + the PWM synthesiser in keeloq_pwm. The mfr list
 * comes from the runtime keystore (KeeloqKeys), so this feature is
 * only useful when /hydra/keeloq_keys.txt is present on SD; the entry
 * screen tells the user when no keys are loaded.
 *
 * Complements:
 *   - Record .sub (idx 8) — captures real signals, identifies the mfr
 *   - Replay .sub (idx 6) — replays captured KeeLoq with counter+1
 *   - Send Code (idx 9)   — rc-switch fixed-code TX (no KeeLoq)
 *
 * SELECT exits at any phase. From the final READY screen, UP transmits
 * and the counter auto-advances so successive presses send counter+1,
 * counter+2, etc.
 */

#ifndef HYDRA_SUB_KEELOQ_H
#define HYDRA_SUB_KEELOQ_H

#include <Arduino.h>

namespace SubKeeloq {

void subKeeloqSetup();
void subKeeloqLoop();

}  // namespace SubKeeloq

#endif
