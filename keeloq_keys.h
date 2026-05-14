/*
 * KeeLoq manufacturer keystore — runtime-loadable.
 *
 * Hydra ships with an empty keystore. KeeLoq decoding/encoding is only
 * useful when the table is populated with manufacturer keys — and those
 * keys are intentionally not redistributed (Flipper protects theirs in
 * silicon; see comments in Momentum's `subghz_keystore.c`). Users who
 * have access to a decrypted keystore (their own Flipper, etc.) drop a
 * plain-text file on the SD card and Hydra picks it up.
 *
 * File format — one entry per line, matching Momentum's decrypted-line
 * format from `subghz_keystore.c::subghz_keystore_process_line()`:
 *
 *   <16-hex-key>:<learning-type-decimal>:<name>
 *
 * Example:
 *   ABCDEF0123456789:2:Example_Vendor
 *   1122334455667788:5:Faac_SLH_test
 *
 * Blank lines and lines starting with `#` are ignored. The learning-type
 * number is one of the values in Keeloq::LearningType (see
 * keeloq_common.h).
 */

#ifndef HYDRA_KEELOQ_KEYS_H
#define HYDRA_KEELOQ_KEYS_H

#include <stdint.h>
#include <stddef.h>

namespace KeeloqKeys {

struct Entry {
  uint64_t mfrKey;        // 64-bit manufacturer key
  uint8_t  learningType;  // Keeloq::LearningType enum value
  char     name[32];      // displayed when this mfr matches a capture
};

// Hard upper bound on loaded entries. 200 mfrs × 48 bytes/entry =
// 9.6 KB BSS — comfortable on ESP32's heap budget.
constexpr int  MAX_ENTRIES        = 200;
constexpr const char* DEFAULT_KEYS_PATH = "/hydra/keeloq_keys.txt";

// Accessors. count() is cheap; at() is bounds-checked and returns a
// reference to a static dummy entry if idx is out of range so callers
// don't have to null-check.
int          count();
const Entry& at(int idx);

// Mutators.
void clear();
bool addEntry(uint64_t mfrKey, uint8_t learningType, const char* name);

// Load entries from an SD file. Clears the existing table first.
// Returns the number of entries successfully loaded, or -1 if the file
// could not be opened. SD mount is performed inside (VSPI re-pin +
// SD.begin(5)).
int loadFromSd(const char* path = DEFAULT_KEYS_PATH);

// Convenience — has the table been loaded at least once this session?
// (Lazy-load helper for features that don't want to call loadFromSd
// every time.)
bool isLoaded();

}  // namespace KeeloqKeys

#endif  // HYDRA_KEELOQ_KEYS_H
