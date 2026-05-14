/*
 * KeeLoq frame decoder — see header.
 *
 * The keystore iteration here is the same shape as Momentum's
 * `subghz_protocol_keeloq_check_remote_controller_selector` in
 * lib/subghz/protocols/keeloq.c, just stripped down to the
 * algorithm essentials and using Hydra's runtime keystore.
 */

#include "keeloq_decode.h"

#include "keeloq_common.h"
#include "keeloq_keys.h"

namespace KeeloqDecode {

namespace {

// Derive the per-device 64-bit key from a manufacturer entry, given
// the frame's serial / button. `seed` is only used by the Secure
// and FAAC learning types — callers pass 0 for now (proper seed
// recovery from frame tails is a milestone 3b concern).
uint64_t deriveDeviceKey(const KeeloqKeys::Entry& mfr,
                         uint32_t serial,
                         uint32_t seed) {
  switch (mfr.learningType) {
    case Keeloq::LearningSimple:
      // Per-device key == mfr key. No derivation step.
      return mfr.mfrKey;
    case Keeloq::LearningNormal:
      return Keeloq::normalLearning(serial, mfr.mfrKey);
    case Keeloq::LearningSecure:
      return Keeloq::secureLearning(serial, seed, mfr.mfrKey);
    case Keeloq::LearningMagicXorType1:
      return Keeloq::magicXorType1Learning(serial, mfr.mfrKey);
    case Keeloq::LearningFaac:
      return Keeloq::faacLearning(seed, mfr.mfrKey);
    case Keeloq::LearningMagicSerialType1:
      return Keeloq::magicSerialType1Learning(serial, mfr.mfrKey);
    case Keeloq::LearningMagicSerialType2:
      return Keeloq::magicSerialType2Learning(serial, mfr.mfrKey);
    case Keeloq::LearningMagicSerialType3:
      return Keeloq::magicSerialType3Learning(serial, mfr.mfrKey);
    case Keeloq::LearningSimpleKingGates:
      // KingGates uses the same encrypt as Simple but the encoder
      // applies extra framing; key derivation is identical.
      return mfr.mfrKey;
    case Keeloq::LearningNormalJarolift:
      // Jarolift uses the Normal derivation but with a different
      // serial pre-processing (LSB-vs-MSB bit reversal). The
      // pre-processing happens at PWM-parse time in milestone 3b;
      // here we just call normalLearning.
      return Keeloq::normalLearning(serial, mfr.mfrKey);
    default:
      // Unknown type — treat as Simple, fail the sanity check below.
      return mfr.mfrKey;
  }
}

// "Sane match" check: the low 10 bits of the unencrypted serial
// inside the decrypted hop must match the low 10 bits of the serial
// transmitted in the clear. This is the canonical KeeLoq receiver-
// side check and is what HCS301 datasheet calls "discrimination".
bool decryptionLooksValid(uint32_t decryptedHop, uint32_t serial) {
  uint32_t hopSerialLow = (decryptedHop >> 16) & 0x3FFu;  // 10 bits
  uint32_t serialLow    = serial & 0x3FFu;
  return hopSerialLow == serialLow;
}

}  // namespace

Result tryDecode(const Frame& frame) {
  Result r = {};
  r.ok = false;

  // No keys loaded → nothing to try. Caller should have called
  // KeeloqKeys::loadFromSd() first; this guard just keeps the error
  // path obvious.
  int n = KeeloqKeys::count();
  if (n <= 0) return r;

  for (int i = 0; i < n; i++) {
    const KeeloqKeys::Entry& mfr = KeeloqKeys::at(i);

    // For now, pass seed=0 for Secure/FAAC variants. Frames that
    // carry a seed tail (the extra 32 bits some mfrs append) will
    // be handled when the PWM parser surfaces that field.
    uint64_t devKey   = deriveDeviceKey(mfr, frame.serial, 0);
    uint32_t decHop   = Keeloq::decrypt(frame.encryptedHop, devKey);

    if (decryptionLooksValid(decHop, frame.serial)) {
      r.ok            = true;
      r.mfrName       = mfr.name;
      r.learningType  = mfr.learningType;
      r.deviceKey     = devKey;
      r.decryptedHop  = decHop;
      r.serial        = frame.serial;
      r.button        = frame.button;
      r.counter       = (uint16_t)(decHop & 0xFFFFu);
      return r;
    }
  }

  return r;
}

uint32_t encodeWithCounter(const Result& matched, uint16_t newCounter) {
  // Rebuild the cleartext hop in the canonical layout:
  //   bits 31..28 = button (4)
  //   bits 27..16 = serial low 10 + 2 status bits (we preserve what
  //                 the decryption gave us so re-encrypting the
  //                 unchanged-except-counter hop produces a valid
  //                 transmission)
  //   bits 15..0  = counter (16)
  // The button can either be re-injected here or kept identical to
  // the captured one; we keep it identical for "counter+1 replay".
  uint32_t cleartextHop = (matched.decryptedHop & 0xFFFF0000u) | newCounter;
  return Keeloq::encrypt(cleartextHop, matched.deviceKey);
}

}  // namespace KeeloqDecode
