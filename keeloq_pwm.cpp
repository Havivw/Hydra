/*
 * KeeLoq PWM frame parser — see header.
 */

#include "keeloq_pwm.h"

#include <stdlib.h>

namespace KeeloqPwm {

namespace {

// HCS301 expects 66 data bits per transmission. A few real-world
// brands extend the tail (status bits, seed) but the core 66 is the
// minimum required for a useful match attempt.
constexpr int   KEELOQ_BITS         = 66;

// Calibration window for te_short: typical HCS301 is 400 µs but
// brands run anywhere from 300 µs to 600 µs.
constexpr int   TE_SHORT_MIN_US     = 200;
constexpr int   TE_SHORT_MAX_US     = 800;

// Tolerance fraction on each timing comparison — KeeLoq receivers
// accept fairly wide windows in practice. ±50% is generous but
// covers cheap remotes with drifty oscillators.
constexpr int   TIMING_TOLERANCE_PC = 50;

// Minimum preamble pulses we need to see before we trust the te_short
// estimate. Most HCS301 remotes emit 12; we'll accept down to 8.
constexpr int   MIN_PREAMBLE_PAIRS  = 8;

// A "long gap" between preamble and data is at least this many
// te_short units of LOW. HCS301 datasheet says ~10×, we accept ≥6.
constexpr int   MIN_HEADER_GAP_TE   = 6;

bool nearly(int a, int target) {
  if (target <= 0) return false;
  int diff = abs(a - target);
  return diff * 100 <= target * TIMING_TOLERANCE_PC;
}

// One step through the sample buffer. Returns the absolute pulse
// duration in µs and writes the level (true = HIGH, false = LOW)
// to *level. Returns 0 once we've walked off the end of the buffer.
int nextPulse(const int* samples, int sampleCount, int& idx, bool& level) {
  if (idx >= sampleCount) return 0;
  int v = samples[idx++];
  level = (v >= 0);
  int dur = (v >= 0) ? v : -v;
  return dur;
}

// Try to decode 66 bits starting at sample index `start` using a
// previously-measured te_short. `invert` flips the PWM convention.
// Returns true if 66 well-formed bits were read; fills outBits[0..65]
// with 0/1 values in transmission order.
bool readBitStream(const int* samples, int sampleCount,
                   int start, int teShort, bool invert,
                   uint8_t outBits[KEELOQ_BITS]) {
  int idx = start;
  int teLong = teShort * 2;
  int bitsRead = 0;

  while (bitsRead < KEELOQ_BITS && idx + 1 < sampleCount) {
    int hi = samples[idx];
    int lo = samples[idx + 1];
    if (hi < 0 || lo >= 0) return false;  // out-of-phase samples
    int hiDur = hi;
    int loDur = -lo;
    idx += 2;

    bool bitVal;
    if (nearly(hiDur, teShort) && nearly(loDur, teLong)) {
      bitVal = invert ? 0 : 1;
    } else if (nearly(hiDur, teLong) && nearly(loDur, teShort)) {
      bitVal = invert ? 1 : 0;
    } else {
      return false;
    }
    outBits[bitsRead++] = bitVal ? 1 : 0;
  }
  return bitsRead == KEELOQ_BITS;
}

// Pack the 66-bit LSB-first stream into the Frame's fields.
void packFrame(const uint8_t bits[KEELOQ_BITS],
               KeeloqDecode::Frame& out) {
  uint32_t hop = 0;
  for (int i = 0; i < 32; i++) hop |= (uint32_t)bits[i] << i;

  uint32_t serial = 0;
  for (int i = 0; i < 28; i++) serial |= (uint32_t)bits[32 + i] << i;

  uint8_t button = 0;
  for (int i = 0; i < 4; i++) button |= (uint8_t)(bits[60 + i] << i);

  uint8_t status = (uint8_t)(bits[64] | (bits[65] << 1));

  out.encryptedHop = hop;
  out.serial       = serial;
  out.button       = button;
  out.status       = status;
}

}  // namespace

bool parseFrame(const int* samples, int sampleCount,
                KeeloqDecode::Frame& outFrame) {
  if (!samples || sampleCount < 80) return false;  // way too short for KeeLoq

  // ---------- Phase 1: find a preamble and estimate te_short ----------
  // Walk the buffer in pairs. A preamble pair is (HIGH ≈ te, LOW ≈ te)
  // where te is in our acceptable band. Once we have MIN_PREAMBLE_PAIRS
  // consecutive matches, the median te becomes our calibration.
  int teEst = 0;
  int preambleEnd = -1;

  for (int probe = 0; probe + 2 * MIN_PREAMBLE_PAIRS < sampleCount; probe++) {
    // Try `probe` as the start of a preamble.
    int hi0 = samples[probe];
    if (hi0 <= 0) continue;
    int candidateTe = hi0;
    if (candidateTe < TE_SHORT_MIN_US || candidateTe > TE_SHORT_MAX_US) continue;

    bool ok = true;
    int sumDur = 0, samples_seen = 0;
    for (int p = 0; p < MIN_PREAMBLE_PAIRS; p++) {
      int hi = samples[probe + 2 * p];
      int lo = samples[probe + 2 * p + 1];
      if (hi <= 0 || lo >= 0) { ok = false; break; }
      int hiDur = hi, loDur = -lo;
      if (!nearly(hiDur, candidateTe) || !nearly(loDur, candidateTe)) {
        ok = false; break;
      }
      sumDur += hiDur + loDur;
      samples_seen += 2;
    }
    if (ok) {
      teEst = sumDur / samples_seen;
      // Skip past the matched preamble — we may have more preamble
      // pulses than MIN; keep advancing while they look like te.
      int i = probe + 2 * MIN_PREAMBLE_PAIRS;
      while (i + 1 < sampleCount) {
        int hi = samples[i], lo = samples[i + 1];
        if (hi <= 0 || lo >= 0) break;
        if (!nearly(hi, teEst) || !nearly(-lo, teEst)) break;
        i += 2;
      }
      preambleEnd = i;
      break;
    }
  }
  if (preambleEnd < 0 || teEst <= 0) return false;

  // ---------- Phase 2: find header gap (long LOW) ----------
  // After the preamble ends, the next sample should be a long LOW.
  // Some remotes have the gap as part of the last preamble LOW; we
  // accept either shape.
  int idx = preambleEnd;
  if (idx >= sampleCount) return false;
  if (samples[idx] < 0) {
    // We're at a LOW — check if it's long enough.
    int gap = -samples[idx];
    if (gap < teEst * MIN_HEADER_GAP_TE) {
      // Not a header gap; advance by one and try.
      idx++;
    } else {
      idx++;
    }
  }
  if (idx >= sampleCount) return false;

  // ---------- Phase 3: read 66 bits ----------
  uint8_t bits[KEELOQ_BITS];
  // Try canonical PWM convention first, then inverted.
  if (readBitStream(samples, sampleCount, idx, teEst, /*invert=*/false, bits) ||
      readBitStream(samples, sampleCount, idx, teEst, /*invert=*/true,  bits)) {
    packFrame(bits, outFrame);
    return true;
  }
  return false;
}

}  // namespace KeeloqPwm
