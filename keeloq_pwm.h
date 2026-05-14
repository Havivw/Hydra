/*
 * KeeLoq PWM frame parser
 *
 * Walks a signed-microsecond sample buffer (Hydra's standard format —
 * positive value = HIGH duration µs, negative = LOW duration µs, as
 * produced by sub_record's CC1101 OOK capture) and tries to recognise
 * a canonical HCS301 / KeeLoq 66-bit transmission.
 *
 * HCS301 framing this parser expects:
 *   - Preamble: ≥8 alternating HIGH/LOW pulses of ~te_short (typ 400µs)
 *   - Header gap: a long LOW (~10× te_short) separating preamble from data
 *   - Data: 66 bits in PWM-3 encoding, each bit = a (HIGH, LOW) pair
 *     whose summed duration ≈ 3 × te_short:
 *         "0" → HIGH long  (2 × te_short) + LOW short (1 × te_short)
 *         "1" → HIGH short (1 × te_short) + LOW long  (2 × te_short)
 *     (HCS301 datasheet PWM-3 convention; if a real-world remote uses
 *     the inverted convention the parser also tries that.)
 *
 * Bit-stream layout (transmitted LSB-first per HCS301):
 *     bits  0..31  encrypted hop  (32 bits)
 *     bits 32..59  serial         (28 bits)
 *     bits 60..63  button code    (4 bits)
 *     bit  64      VLOW flag
 *     bit  65      RPT flag
 *
 * Returns true if a plausible frame was recovered. Caller hands the
 * filled-in KeeloqDecode::Frame to KeeloqDecode::tryDecode().
 *
 * Out of scope here: Manchester-encoded variants (Hörmann BiSecur,
 * NICE FloR-S secure tail), multi-frame captures, seed-tail
 * extraction. Those can land later as keeloq_pwm_<variant>.
 */

#ifndef HYDRA_KEELOQ_PWM_H
#define HYDRA_KEELOQ_PWM_H

#include <stdint.h>

#include "keeloq_decode.h"

namespace KeeloqPwm {

// Parse the first plausible KeeLoq frame in `samples`. On success the
// supplied frame struct is populated and true is returned. Failure
// reasons (no preamble, wrong bit count, mis-shaped bits) are not
// surfaced — caller treats "false" as "this capture isn't KeeLoq".
bool parseFrame(const int* samples, int sampleCount,
                KeeloqDecode::Frame& outFrame);

// Synthesise a PWM sample stream from a Frame. The output is the same
// signed-microsecond format the parser consumes (positive=HIGH µs,
// negative=LOW µs), suitable for handing to the existing CC1101 OOK
// bit-bang TX path. `teShortUs` controls the symbol time (typ 400 µs
// for HCS301; pass the value captured during decode for accuracy).
//
// Layout produced:
//   - 12 preamble pairs of (te, te)
//   - one header gap (LOW for ~10 × te)
//   - 66 PWM-3 data bits, LSB-first per HCS301 framing
//
// Returns the number of samples written (≈ 160), or 0 if `maxSamples`
// is too small.
int buildFrame(const KeeloqDecode::Frame& frame,
               int teShortUs,
               int* outSamples,
               int maxSamples);

}  // namespace KeeloqPwm

#endif  // HYDRA_KEELOQ_PWM_H
