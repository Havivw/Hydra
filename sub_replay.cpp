/*
 * Flipper .sub file replay — see header.
 */

#include "sub_replay.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"
#include "subconfig.h"
#include "shared.h"
#include "sub_shared.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include <stdlib.h>

// Button pins, DARK_GRAY, and CC1101_GDO0_PIN come from shared.h + sub_shared.h.

namespace SubReplay {

enum Phase { PHASE_LIST = 0, PHASE_INFO, PHASE_TX, PHASE_DONE, PHASE_ERROR };

#define MAX_FILES 30
#define MAX_FILENAME 40
struct SubFile { char name[MAX_FILENAME]; };
static SubFile files[MAX_FILES];
static int fileCount = 0;

// Selection cursor in the LIST phase
static int cursor = 0;
static int viewOffset = 0;
#define LIST_VISIBLE_ROWS 13
#define LIST_ROW_HEIGHT 16
#define LIST_TOP_Y 60

// Parsed signal. 2000 samples = 8 KB; covers the vast majority of public
// Flipper RAW captures (typical files are 50-1500 samples). Bumping higher
// pushes DRAM over the line on a board this loaded — if you have a longer
// signal, split it into multiple .sub files.
#define MAX_SAMPLES 2000
static int      sampleBuf[MAX_SAMPLES];
static int      sampleCount = 0;
static uint32_t parsedFreqHz = 0;
enum PresetKind { PRESET_UNKNOWN = 0, PRESET_OOK_650 = 1, PRESET_OOK_270 = 2 };
static int parsedPreset = PRESET_UNKNOWN;
static char loadedName[MAX_FILENAME] = "";
static char errorMsg[64] = "";

// Phase state
static Phase phase = PHASE_LIST;
static bool  sdReady = false;
static uint32_t txStartedAt = 0;
static int  txIdx = 0;
static uint32_t lastNavMs = 0;
static const uint32_t NAV_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;

// ============================================================
// SD scan
// ============================================================
static void scanSubDir() {
  fileCount = 0;
  if (!SD.exists("/hydra")) SD.mkdir("/hydra");
  if (!SD.exists("/hydra/sub_files")) SD.mkdir("/hydra/sub_files");

  File dir = SD.open("/hydra/sub_files");
  if (!dir) return;
  if (!dir.isDirectory()) { dir.close(); return; }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      const char* name = entry.name();
      // SD library may give name with or without prefix — strip to basename
      const char* slash = strrchr(name, '/');
      if (slash) name = slash + 1;
      int len = strlen(name);
      if (len >= 4 && strcmp(name + len - 4, ".sub") == 0 && fileCount < MAX_FILES) {
        int n = len;
        if (n >= MAX_FILENAME) n = MAX_FILENAME - 1;
        memcpy(files[fileCount].name, name, n);
        files[fileCount].name[n] = '\0';
        fileCount++;
      }
    }
    entry.close();
  }
  dir.close();
}

// ============================================================
// .sub parser
// ============================================================
static int classifyPreset(const char* s) {
  if (strstr(s, "Ook650Async")) return PRESET_OOK_650;
  if (strstr(s, "Ook270Async")) return PRESET_OOK_270;
  return PRESET_UNKNOWN;
}

// Parse one RAW_Data line: starts with "RAW_Data:" then space-separated
// signed ints. Append to sampleBuf until full.
static void parseRawDataLine(const char* line) {
  // Skip past "RAW_Data:"
  const char* p = strchr(line, ':');
  if (!p) return;
  p++;
  while (*p && sampleCount < MAX_SAMPLES) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p == '\r' || *p == '\n') break;
    char* end;
    long v = strtol(p, &end, 10);
    if (end == p) break;
    sampleBuf[sampleCount++] = (int)v;
    p = end;
  }
}

static bool loadFile(const char* filename) {
  sampleCount = 0;
  parsedFreqHz = 0;
  parsedPreset = PRESET_UNKNOWN;
  errorMsg[0] = '\0';

  char path[80];
  snprintf(path, sizeof(path), "/hydra/sub_files/%s", filename);
  File f = SD.open(path, FILE_READ);
  if (!f) { strncpy(errorMsg, "open failed", sizeof(errorMsg)); return false; }

  char line[160];
  int li = 0;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\r') continue;
    if (c == '\n' || li >= (int)sizeof(line) - 1) {
      line[li] = '\0';

      if (strncmp(line, "Frequency:", 10) == 0) {
        parsedFreqHz = (uint32_t)strtoul(line + 10, nullptr, 10);
      } else if (strncmp(line, "Preset:", 7) == 0) {
        parsedPreset = classifyPreset(line + 7);
      } else if (strncmp(line, "RAW_Data:", 9) == 0) {
        parseRawDataLine(line);
      }

      li = 0;
      if (sampleCount >= MAX_SAMPLES) break;
      continue;
    }
    line[li++] = c;
  }
  if (li > 0) {
    line[li] = '\0';
    if (strncmp(line, "RAW_Data:", 9) == 0) parseRawDataLine(line);
  }
  f.close();

  if (parsedPreset == PRESET_UNKNOWN) {
    strncpy(errorMsg, "unsupported preset (only OOK 650/270)", sizeof(errorMsg));
    return false;
  }
  if (parsedFreqHz == 0) {
    strncpy(errorMsg, "missing Frequency:", sizeof(errorMsg));
    return false;
  }
  if (sampleCount == 0) {
    strncpy(errorMsg, "no RAW_Data samples", sizeof(errorMsg));
    return false;
  }

  strncpy(loadedName, filename, MAX_FILENAME - 1);
  loadedName[MAX_FILENAME - 1] = '\0';
  return true;
}

// ============================================================
// CC1101 OOK async TX
// ============================================================
static void cc1101TxPrep() {
  cc1101InitForDivV1();
  ELECHOUSE_cc1101.setMHZ(parsedFreqHz / 1000000.0f);
  ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK
  if (parsedPreset == PRESET_OOK_650) {
    ELECHOUSE_cc1101.setDRate(650);
    ELECHOUSE_cc1101.setRxBW(650);
  } else {
    ELECHOUSE_cc1101.setDRate(270);
    ELECHOUSE_cc1101.setRxBW(270);
  }
  ELECHOUSE_cc1101.setPktFormat(3);   // async serial mode
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();
  pinMode(CC1101_GDO0_PIN, OUTPUT);
}

static void cc1101TxDone() {
  digitalWrite(CC1101_GDO0_PIN, LOW);
  ELECHOUSE_cc1101.setSidle();
}

// Bit-bang all samples. Positive value = HIGH for that many microseconds,
// negative = LOW. Aborts early if SELECT is pressed.
static void bitbangAllSamples() {
  for (txIdx = 0; txIdx < sampleCount; txIdx++) {
    int s = sampleBuf[txIdx];
    int level = (s >= 0) ? HIGH : LOW;
    int dur = s >= 0 ? s : -s;
    digitalWrite(CC1101_GDO0_PIN, level);
    if (dur > 0) delayMicroseconds(dur);

    // Check abort every ~64 samples (cheap PCF8574 read)
    if ((txIdx & 0x3F) == 0 && !pcf.digitalRead(BTN_SELECT)) break;
  }
}

// ============================================================
// LIST phase UI
// ============================================================
static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  if (phase == PHASE_LIST) {
    tft.printf(".sub Files (%d found)  [RIGHT]=pick", fileCount);
  } else if (phase == PHASE_INFO) {
    tft.printf("File: %s", loadedName);
  } else if (phase == PHASE_TX) {
    tft.printf("Transmitting...");
  } else if (phase == PHASE_DONE) {
    tft.print("Done — press any key");
  } else {
    tft.print("Error");
  }
}

static void drawListBody() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_VISIBLE_ROWS * LIST_ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  if (fileCount == 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(2, LIST_TOP_Y);
    tft.print("No .sub files found.");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(2, LIST_TOP_Y + 16);
    tft.print("Put files in:");
    tft.setCursor(2, LIST_TOP_Y + 30);
    tft.print("/hydra/sub_files/");
    return;
  }

  int n = fileCount - viewOffset;
  if (n > LIST_VISIBLE_ROWS) n = LIST_VISIBLE_ROWS;
  for (int s = 0; s < n; s++) {
    int i = viewOffset + s;
    int y = LIST_TOP_Y + s * LIST_ROW_HEIGHT;
    bool hi = (i == cursor);
    tft.setTextColor(hi ? TFT_ORANGE : TFT_WHITE, TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf("%s %s", hi ? ">" : " ", files[i].name);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (viewOffset > 0) {
    tft.setCursor(228, LIST_TOP_Y);
    tft.print("^");
  }
  if (viewOffset + n < fileCount) {
    tft.setCursor(228, LIST_TOP_Y + (n - 1) * LIST_ROW_HEIGHT);
    tft.print("v");
  }
}

static void drawInfo() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, LIST_TOP_Y);
  tft.print("File:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 14);
  tft.print(loadedName);

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, LIST_TOP_Y + 35);
  tft.print("Frequency:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 49);
  tft.printf("%.3f MHz", parsedFreqHz / 1000000.0f);

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, LIST_TOP_Y + 70);
  tft.print("Preset:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 84);
  tft.print(parsedPreset == PRESET_OOK_650 ? "OOK 650 async"
          : parsedPreset == PRESET_OOK_270 ? "OOK 270 async"
                                            : "?");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, LIST_TOP_Y + 105);
  tft.print("Samples:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 119);
  tft.printf("%d", sampleCount);

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(2, LIST_TOP_Y + 150);
  tft.print("[UP]   TRANSMIT");
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, LIST_TOP_Y + 168);
  tft.print("[LEFT] Back to list");
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, LIST_TOP_Y + 186);
  tft.print("[SEL]  Exit feature");
}

static void drawTx() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, LIST_TOP_Y + 20);
  tft.print("Transmitting...");

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 60);
  tft.printf("File: %s", loadedName);
  tft.setCursor(2, LIST_TOP_Y + 76);
  tft.printf("Freq: %.3f MHz", parsedFreqHz / 1000000.0f);
  tft.setCursor(2, LIST_TOP_Y + 92);
  tft.printf("Samples: %d", sampleCount);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, LIST_TOP_Y + 130);
  tft.print("(SELECT to abort)");
}

static void drawDone() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, LIST_TOP_Y + 20);
  tft.print("Transmission complete.");
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 60);
  tft.printf("Sent %d / %d samples", txIdx, sampleCount);
  tft.setCursor(2, LIST_TOP_Y + 80);
  tft.printf("Elapsed: %u ms",
             (unsigned)(millis() - txStartedAt));
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, LIST_TOP_Y + 120);
  tft.print("[ANY] Back to file list");
}

static void drawError() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), tft.height() - LIST_TOP_Y, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.setCursor(10, LIST_TOP_Y + 20);
  tft.print("Cannot use this file");
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP_Y + 60);
  tft.print(errorMsg);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, LIST_TOP_Y + 100);
  tft.print("[LEFT] Back to list");
}

static void ensureCursorVisible() {
  if (cursor < viewOffset) viewOffset = cursor;
  else if (cursor >= viewOffset + LIST_VISIBLE_ROWS) viewOffset = cursor - LIST_VISIBLE_ROWS + 1;
  int maxOff = fileCount - LIST_VISIBLE_ROWS;
  if (maxOff < 0) maxOff = 0;
  if (viewOffset > maxOff) viewOffset = maxOff;
  if (viewOffset < 0) viewOffset = 0;
}

// ============================================================
// PUBLIC API
// ============================================================
void subReplaySetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  // Mount SD first. Touchscreen + NRF24 features earlier in the session
  // may have left VSPI pinned elsewhere; force the bus back onto its
  // canonical pins, then drive SD CS high once to give the card a clean
  // edge before SD.begin() asserts it. See memory feedback_sd_vspi_repin.
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
  delay(5);
  SPI.begin(18, 19, 23, 5);
  sdReady = SD.begin(5);
  if (sdReady) scanSubDir();

  cursor = 0; viewOffset = 0;
  phase = PHASE_LIST;
  toggleHeldFromPrev = true;

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawListBody();
}

void subReplayLoop() {
  uint32_t now = millis();
  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool upPressed    = !pcf.digitalRead(BTN_UP);
  bool downPressed  = !pcf.digitalRead(BTN_DOWN);
  bool anyToggle = leftPressed || rightPressed || upPressed || downPressed;

  // wait-for-release latch
  if (toggleHeldFromPrev) {
    if (!anyToggle) toggleHeldFromPrev = false;
    delay(15);
    return;
  }

  if (phase == PHASE_LIST) {
    bool redraw = false;
    if (downPressed && fileCount > 0 && now - lastNavMs > NAV_DEBOUNCE_MS) {
      cursor = (cursor + 1) % fileCount;
      lastNavMs = now; redraw = true;
    } else if (upPressed && fileCount > 0 && now - lastNavMs > NAV_DEBOUNCE_MS) {
      cursor = (cursor - 1 + fileCount) % fileCount;
      lastNavMs = now; redraw = true;
    } else if (rightPressed && fileCount > 0) {
      // Pick file → parse → INFO (or ERROR)
      if (loadFile(files[cursor].name)) {
        phase = PHASE_INFO;
      } else {
        phase = PHASE_ERROR;
      }
      toggleHeldFromPrev = true;
      drawHeader();
      if (phase == PHASE_INFO) drawInfo(); else drawError();
      return;
    }
    if (redraw) {
      ensureCursorVisible();
      drawListBody();
    }
  }
  else if (phase == PHASE_INFO) {
    if (leftPressed) {
      phase = PHASE_LIST;
      toggleHeldFromPrev = true;
      drawHeader();
      drawListBody();
      return;
    }
    if (upPressed) {
      phase = PHASE_TX;
      drawHeader();
      drawTx();
      cc1101TxPrep();
      txStartedAt = millis();
      bitbangAllSamples();          // blocking — UI freezes during TX, intended
      cc1101TxDone();
      phase = PHASE_DONE;
      toggleHeldFromPrev = true;
      drawHeader();
      drawDone();
      return;
    }
  }
  else if (phase == PHASE_DONE) {
    if (anyToggle) {
      phase = PHASE_LIST;
      toggleHeldFromPrev = true;
      drawHeader();
      drawListBody();
    }
  }
  else if (phase == PHASE_ERROR) {
    if (leftPressed) {
      phase = PHASE_LIST;
      toggleHeldFromPrev = true;
      drawHeader();
      drawListBody();
    }
  }

  delay(15);
}

}  // namespace SubReplay
