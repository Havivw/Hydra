/*
 * KeeLoq cipher primitives — Hydra port. See header.
 *
 * Algorithm faithful to Momentum's keeloq_common.c:
 *   https://github.com/Next-Flip/Momentum-Firmware/blob/dev/lib/subghz/protocols/keeloq_common.c
 *
 * The 528-round inner loop is the only hot path; everything else calls it.
 * On ESP32 @ 240 MHz a single 528-round decrypt takes ~50–100 µs, so a
 * full keystore scan over ~150 mfrs lands around 8–15 ms. Acceptable
 * inline in the Record loop.
 */

#include "keeloq_common.h"

namespace Keeloq {

namespace {

// One-bit accessors — kept as inline helpers exactly like Momentum's
// `bit(x, n)` macro, just type-safe.
inline uint32_t bit32(uint32_t x, int n) { return (x >> n) & 1u; }
inline uint32_t bit64(uint64_t x, int n) { return (uint32_t)((x >> n) & 1u); }

// 5-input gather: pack 5 bits of x into the low 5 bits of a 32-bit value
// in order (a,b,c,d,e). The result indexes into KEELOQ_NLF.
inline uint32_t g5(uint32_t x, int a, int b, int c, int d, int e) {
  return bit32(x, a)        + bit32(x, b) * 2u  +
         bit32(x, c) * 4u   + bit32(x, d) * 8u  +
         bit32(x, e) * 16u;
}

}  // namespace

uint32_t encrypt(uint32_t data, uint64_t key) {
  uint32_t x = data;
  for (int r = 0; r < 528; r++) {
    uint32_t fb = bit32(x, 0) ^ bit32(x, 16) ^ bit64(key, r & 63) ^
                  bit32(NLF, g5(x, 1, 9, 20, 26, 31));
    x = (x >> 1) ^ (fb << 31);
  }
  return x;
}

uint32_t decrypt(uint32_t data, uint64_t key) {
  uint32_t x = data;
  for (int r = 0; r < 528; r++) {
    x = (x << 1) ^ bit32(x, 31) ^ bit32(x, 15) ^
        bit64(key, (15 - r) & 63) ^
        bit32(NLF, g5(x, 0, 8, 19, 25, 30));
  }
  return x;
}

uint64_t normalLearning(uint32_t serial, uint64_t key) {
  uint32_t data;
  data = (serial & 0x0FFFFFFFu) | 0x20000000u;
  uint32_t k1 = decrypt(data, key);
  data = (serial & 0x0FFFFFFFu) | 0x60000000u;
  uint32_t k2 = decrypt(data, key);
  return ((uint64_t)k2 << 32) | k1;
}

uint64_t secureLearning(uint32_t serial, uint32_t seed, uint64_t key) {
  uint32_t k1 = decrypt(serial & 0x0FFFFFFFu, key);
  uint32_t k2 = decrypt(seed, key);
  return ((uint64_t)k1 << 32) | k2;
}

uint64_t magicXorType1Learning(uint32_t serial, uint64_t xorVal) {
  uint32_t s = serial & 0x0FFFFFFFu;
  return (((uint64_t)s << 32) | s) ^ xorVal;
}

uint64_t faacLearning(uint32_t seed, uint64_t key) {
  uint16_t hs = (uint16_t)(seed >> 16);
  const uint16_t ending = 0x544D;  // "TM"
  uint32_t lsb = ((uint32_t)hs << 16) | ending;
  return ((uint64_t)encrypt(seed, key) << 32) | encrypt(lsb, key);
}

uint64_t magicSerialType1Learning(uint32_t serial, uint64_t man) {
  uint32_t s = serial & 0x0FFFFFFFu;
  return (man & 0xFFFFFFFFULL) |
         ((uint64_t)s << 40) |
         ((uint64_t)(((s & 0xFFu) + ((s >> 8) & 0xFFu)) & 0xFFu) << 32);
}

uint64_t magicSerialType2Learning(uint32_t data, uint64_t man) {
  // Overlay the 4 bytes of `data` into the top 4 bytes of `man` (in
  // reverse byte order, matching Momentum's pointer-aliased assignment).
  uint8_t *p = (uint8_t *)&data;
  uint8_t *m = (uint8_t *)&man;
  m[7] = p[0]; m[6] = p[1]; m[5] = p[2]; m[4] = p[3];
  return man;
}

uint64_t magicSerialType3Learning(uint32_t data, uint64_t man) {
  return (man & 0xFFFFFFFFFF000000ULL) | (data & 0xFFFFFFu);
}

}  // namespace Keeloq
