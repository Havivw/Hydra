/*
 * KeeLoq manufacturer keystore — see header.
 */

#include "keeloq_keys.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace KeeloqKeys {

namespace {

Entry entries[MAX_ENTRIES];
int   entryCount = 0;
bool  loadedOnce = false;

// Returned when at() is called with an out-of-range index. Marking it
// static keeps the symbol private; const so callers can't accidentally
// scribble through it.
const Entry kEmpty = { 0, 0, "" };

// Trim leading whitespace in-place. Returns pointer to first non-WS char.
char* lstrip(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

// Drop trailing CR/LF/spaces in-place.
void rstrip(char* s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                   s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
}

}  // namespace

int count() { return entryCount; }

const Entry& at(int idx) {
  if (idx < 0 || idx >= entryCount) return kEmpty;
  return entries[idx];
}

void clear() {
  entryCount = 0;
  // Don't reset loadedOnce — if the user explicitly clears, they may
  // still want subsequent calls to treat the table as "user has been
  // here" so a feature doesn't auto-reload from SD.
}

bool addEntry(uint64_t mfrKey, uint8_t learningType, const char* name) {
  if (entryCount >= MAX_ENTRIES) return false;
  Entry& e = entries[entryCount++];
  e.mfrKey = mfrKey;
  e.learningType = learningType;
  size_t n = strlen(name);
  if (n >= sizeof(e.name)) n = sizeof(e.name) - 1;
  memcpy(e.name, name, n);
  e.name[n] = '\0';
  return true;
}

int loadFromSd(const char* path) {
  clear();

  // VSPI re-pin in case a prior CC1101/NRF feature left the bus pinned
  // elsewhere. Same pattern SubReplay and SubRecord use.
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
  delay(5);
  SPI.begin(18, 19, 23, 5);

  if (!SD.begin(5)) {
    loadedOnce = true;  // we tried; the absence of a file is "loaded as empty"
    return -1;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    loadedOnce = true;
    return -1;
  }

  char line[160];
  int li = 0;
  int loaded = 0;

  while (f.available()) {
    char c = (char)f.read();
    if (c == '\r') continue;

    if (c == '\n' || li >= (int)sizeof(line) - 1) {
      line[li] = '\0';
      char* p = lstrip(line);
      rstrip(p);

      if (*p && *p != '#') {
        char keyStr[17] = {0};
        unsigned int type = 0;
        char nameStr[40] = {0};
        // %32[^\n] reads to end-of-line, allowing names with spaces.
        // strtoull tolerates the case-insensitive hex we matched here.
        if (sscanf(p, "%16[0-9A-Fa-f]:%u:%32[^\n]",
                   keyStr, &type, nameStr) == 3) {
          uint64_t k = strtoull(keyStr, nullptr, 16);
          // Strip trailing CR if it slipped past (rstrip above handled
          // the line itself, but nameStr was read by sscanf separately).
          rstrip(nameStr);
          if (addEntry(k, (uint8_t)type, nameStr)) loaded++;
        }
      }
      li = 0;
      continue;
    }
    line[li++] = c;
  }
  f.close();
  loadedOnce = true;
  return loaded;
}

bool isLoaded() { return loadedOnce; }

}  // namespace KeeloqKeys
