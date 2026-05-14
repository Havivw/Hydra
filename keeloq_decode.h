/*
 * KeeLoq frame decoder — keystore-iterating "try every key" core.
 *
 * Given a parsed KeeLoq transmission (32-bit encrypted hop + 28-bit
 * serial + 4-bit button + 2-bit status, per the canonical HCS301
 * framing), this module walks the loaded KeeloqKeys table, derives a
 * per-device key for each candidate mfr via the appropriate learning
 * function, decrypts the hop, and returns the first mfr whose
 * decryption yields a sane (counter, serial-low-bits) pair.
 *
 * "Sane" here means: the low 10 bits of the serial inside the
 * decrypted hop match the low 10 bits of the unencrypted serial in
 * the same frame. That self-consistency check is the standard way
 * to detect a correct key — a wrong key produces a random-looking
 * decryption that almost certainly fails the check (~1 in 1024
 * false-positive rate per key tried; with ~200 keys total we expect
 * <1 false positive per real-world scan).
 *
 * Edge bit-level parsing (signed-microsecond samples → 66-bit
 * KeeloqFrame) lives in keeloq_pwm.{h,cpp} — added in milestone 3b.
 */

#ifndef HYDRA_KEELOQ_DECODE_H
#define HYDRA_KEELOQ_DECODE_H

#include <stdint.h>

namespace KeeloqDecode {

// Parsed-but-not-yet-decrypted KeeLoq frame. Filled by the PWM
// parser (milestone 3b) or hand-built by tests/manual entry.
struct Frame {
  uint32_t encryptedHop;   // 32 bits — the rolling encrypted portion
  uint32_t serial;         // 28 bits — least-significant 28 bits used
  uint8_t  button;         // 4 bits  — S0..S3 button code
  uint8_t  status;         // 2 bits  — VLOW/RPT flags, mfr-specific use
};

// Result of a successful keystore match.
struct Result {
  bool        ok;              // false if no key in the table matched
  const char* mfrName;         // pointer into KeeloqKeys table; valid until clear()
  uint8_t     learningType;    // which Keeloq::LearningType matched
  uint64_t    deviceKey;       // per-device key after learning derivation
  uint32_t    decryptedHop;    // raw 32-bit decrypt of frame.encryptedHop
  uint32_t    serial;          // copy of frame.serial (for caller convenience)
  uint8_t     button;          // copy of frame.button
  uint16_t    counter;         // low 16 bits of decryptedHop
};

// Try every key in the KeeloqKeys table against `frame`. Returns
// {ok=true, ...} on first match. If no match, returns {ok=false}.
// Cost: ~150 keys × ~100us per 528-round decrypt = ~15ms typical.
Result tryDecode(const Frame& frame);

// Re-encrypt with a new counter — used by SubReplay's "Replay
// counter+1" path. Takes a previously-decoded Result, bumps its
// counter by `delta`, and rebuilds the encrypted hop. Returns the
// new 32-bit encryptedHop suitable for transmission via the same
// PWM emitter used for normal replay.
uint32_t encodeWithCounter(const Result& matched, uint16_t newCounter);

}  // namespace KeeloqDecode

#endif  // HYDRA_KEELOQ_DECODE_H
