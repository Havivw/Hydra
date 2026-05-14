/*
 * KeeLoq cipher primitives — Hydra port of Momentum's keeloq_common.{c,h}.
 *
 * Reference:
 *   https://github.com/Next-Flip/Momentum-Firmware/blob/dev/lib/subghz/protocols/keeloq_common.c
 *
 * KeeLoq is a 32-bit block cipher with a 64-bit key, used in rolling-code
 * keyfobs. The algorithm itself is small (528 rounds of a 5-input NLFSR);
 * the value of a port lives in the manufacturer-key table that maps every
 * known mfr+variant to its 64-bit key + which "learning" function to use
 * to derive the per-device key from (serial, mfr_key).
 *
 * This file provides only the algorithm primitives. The keystore lives in
 * keeloq_keys.{h,cpp} (next milestone) and the decoder/encoder pipeline
 * lives in keeloq_decode.{h,cpp} (after that).
 *
 * Public API mirrors Momentum's, but renamed into Hydra's Keeloq::
 * namespace and adjusted to C++ idioms (constexpr, no inline-on-extern).
 */

#ifndef HYDRA_KEELOQ_COMMON_H
#define HYDRA_KEELOQ_COMMON_H

#include <stdint.h>

namespace Keeloq {

// KeeLoq Non-Linear Function — the cipher's S-box-equivalent. The 5-input
// NLFSR samples one of these 32 bits each round; the choice is driven by
// the current g5() value of the data register.
constexpr uint32_t NLF = 0x3A5C742E;

// Learning types — different manufacturer derive the per-device key from
// the serial/seed in different ways. Values match Momentum's enum so that
// the keystore entries can be lifted verbatim without remapping.
enum LearningType : uint8_t {
  LearningUnknown          = 0,
  LearningSimple           = 1,
  LearningNormal           = 2,
  LearningSecure           = 3,
  LearningMagicXorType1    = 4,
  LearningFaac             = 5,
  LearningMagicSerialType1 = 6,
  LearningMagicSerialType2 = 7,
  LearningMagicSerialType3 = 8,
  // 9 reserved by Momentum for Beninca ARC, not used standalone
  LearningSimpleKingGates  = 10,
  LearningNormalJarolift   = 11,
};

// Core block cipher. data = 0xBSSSCCCC where B=button(4), S=serial(10),
// C=counter(16). key is the per-device 64-bit manufacture key. Both
// directions are pure functions of (data, key); encrypt and decrypt are
// inverses under the same key.
uint32_t encrypt(uint32_t data, uint64_t key);
uint32_t decrypt(uint32_t data, uint64_t key);

// Key-derivation functions. Each takes the (28-bit) serial — or, in some
// variants, a seed value or an opaque "magic" value — plus the mfr key,
// and returns the per-device 64-bit key used to encrypt that remote's
// rolling code.
uint64_t normalLearning          (uint32_t serial, uint64_t key);
uint64_t secureLearning          (uint32_t serial, uint32_t seed, uint64_t key);
uint64_t magicXorType1Learning   (uint32_t serial, uint64_t xorVal);
uint64_t faacLearning            (uint32_t seed,   uint64_t key);
uint64_t magicSerialType1Learning(uint32_t serial, uint64_t man);
uint64_t magicSerialType2Learning(uint32_t data,   uint64_t man);
uint64_t magicSerialType3Learning(uint32_t data,   uint64_t man);

}  // namespace Keeloq

#endif  // HYDRA_KEELOQ_COMMON_H
