/*
 * SubGHz raw RX capture — see header.
 */

#include "sub_record.h"
#include "wardrive_config.h"
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
#include <stdio.h>

namespace SubRecord {

// ============================================================
// State
// ============================================================
enum Phase {
  PHASE_PICK = 0,   // choosing a frequency
  PHASE_LISTEN,     // RX armed, waiting for first edge
  PHASE_DONE,       // capture complete, awaiting save/discard
  PHASE_SAVED,      // showed filename, waiting for any-button to return
  PHASE_ERROR       // SD or other failure, any-button to return
};

// Storage cap. Each sample is a signed 32-bit microsecond duration.
// Backed by the shared SubGHz buffer in sub_shared.h (also used by
// SubReplay for playback) so we don't burn another 8 KB of DRAM.
#define MAX_SAMPLES SUB_SAMPLE_BUF_MAX
#define sampleBuf   subSampleBuf

// Silence threshold — stop recording if no edges seen for this long after
// recording started. 250 ms is long enough for the gap between repeats in
// a typical keyfob burst, short enough that the user gets immediate result.
#define SILENCE_TIMEOUT_US 250000UL

// Hard upper bound on listen time before we bail with "no signal". Keeps
// the UI responsive — if nothing useful happens in 30 s the user almost
// certainly wants to move the transmitter closer or switch frequency.
#define LISTEN_TIMEOUT_MS 30000UL

// Hard cap on a single sample's duration. CC1101 GDO0 can stay HIGH/LOW
// for far longer than any real OOK symbol when the radio is idle; capping
// avoids one giant "-2000000" sample dominating the file.
#define MAX_SAMPLE_US 32000

static Phase phase = PHASE_PICK;
static int  cursor = 0;
static int  viewOffset = 0;
static uint16_t activeIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int activeCount = 0;
static int chosenChannelIdx = -1;
static int      sampleCount = 0;

// rc-switch decode result. decodedProtocol == 0 means no recognised
// protocol; the .sub file still gets saved with RAW samples in that
// case. Protocol IDs 1-12 are the ones rc-switch ships with — covers
// Princeton/PT2262, EV1527, HT12, HT6/HT8, Conrad RS-200, and a few
// variants common on cheap 433 MHz keyfobs and door sensors.
static uint32_t decodedValue = 0;
static int      decodedBitLength = 0;
static int      decodedProtocol = 0;

static const char* protocolName(int p) {
  switch (p) {
    case 1:  return "PT2262 / Princeton";
    case 2:  return "HT12 / EV1527";
    case 3:  return "Protocol 3";
    case 4:  return "PT2262 variant";
    case 5:  return "500us short";
    case 6:  return "HT6 / HT8";
    case 7:  return "23x sync";
    case 8:  return "Conrad RS-200";
    case 9:  return "365us short";
    case 10: return "270us short";
    case 11: return "320us short";
    case 12: return "400us short";
    default: return "RAW only";
  }
}
static uint32_t listenStartMs = 0;
static char     savedName[64] = "";
static char     errorMsg[80] = "";
static uint32_t lastBtnMs = 0;
static bool     toggleHeldFromPrev = true;
static const uint32_t NAV_DEBOUNCE_MS = 180;

#define LIST_VISIBLE_ROWS 11
#define LIST_ROW_HEIGHT   16
#define LIST_TOP_Y        65

// ============================================================
// Active-channel builder (mirrors SubReplay/SweepJammer pattern)
// ============================================================
static void rebuildActive() {
  activeCount = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (WardriveConfig::channels[i].selected) {
      activeIdx[activeCount++] = i;
    }
  }
  if (activeCount == 0) {
    // Fall back to all 30 channels so the user can still pick something.
    for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) activeIdx[i] = i;
    activeCount = HYDRA_WARDRIVE_CHANNEL_COUNT;
  }
  cursor = 0;
  viewOffset = 0;
}

// ============================================================
// SD: next available rec_NNN.sub filename
// ============================================================
static bool ensureRecDir() {
  if (!SD.exists("/hydra")) SD.mkdir("/hydra");
  if (!SD.exists("/hydra/sub_files")) SD.mkdir("/hydra/sub_files");
  return true;
}

// Scan /hydra/sub_files for files matching rec_NNN.sub and pick N+1. The
// scan only looks at the numeric portion; out-of-range names are ignored.
static void nextRecFilename(char *out, size_t outSize) {
  int best = 0;
  File dir = SD.open("/hydra/sub_files");
  if (dir && dir.isDirectory()) {
    File entry;
    while ((entry = dir.openNextFile())) {
      if (!entry.isDirectory()) {
        const char *name = entry.name();
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
        if (strncmp(name, "rec_", 4) == 0) {
          int n = atoi(name + 4);
          if (n > best) best = n;
        }
      }
      entry.close();
    }
    dir.close();
  }
  snprintf(out, outSize, "/hydra/sub_files/rec_%03d.sub", best + 1);
}

// ============================================================
// CC1101 RX OOK async — open-the-door, listen, close-the-door
// ============================================================
static void cc1101RxPrep(uint32_t freqHz) {
  cc1101InitForDivV1();
  ELECHOUSE_cc1101.setMHZ(freqHz / 1000000.0f);
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setDRate(650);
  ELECHOUSE_cc1101.setRxBW(650);
  ELECHOUSE_cc1101.setPktFormat(3);    // async serial — raw demod on GDO0/GDO2
  ELECHOUSE_cc1101.SetRx();
  pinMode(CC1101_GDO0_PIN, INPUT);

  // Run rc-switch's ISR-based decoder in parallel with the polling
  // capture. The ISR fires on every GDO0 transition and feeds an
  // internal state machine that recognises ~12 common fixed-code
  // protocols (PT2262/Princeton, EV1527, HT12, HT6/HT8, etc.). Polling
  // and ISR coexist because they each just read the same pin.
  decodedValue = 0;
  decodedBitLength = 0;
  decodedProtocol = 0;
  subSwitch.enableReceive(CC1101_GDO0_PIN);
}

static void cc1101RxDone() {
  // Snapshot anything rc-switch decoded during the capture window before
  // we tear down the ISR.
  if (subSwitch.available()) {
    decodedValue     = subSwitch.getReceivedValue();
    decodedBitLength = subSwitch.getReceivedBitlength();
    decodedProtocol  = subSwitch.getReceivedProtocol();
    subSwitch.resetAvailable();
  }
  subSwitch.disableReceive();
  ELECHOUSE_cc1101.setSidle();
}

// Tight RX loop. Polls GDO0; on each level change, appends a signed
// duration (positive = HIGH, negative = LOW) to sampleBuf. Returns when:
//   - sampleBuf is full
//   - no edge seen for SILENCE_TIMEOUT_US after at least one edge
//   - LISTEN_TIMEOUT_MS elapsed without any edge
//   - user presses SELECT
//
// Returns true if any samples were captured.
static bool captureSamples() {
  sampleCount = 0;
  int  prevLevel = digitalRead(CC1101_GDO0_PIN);
  uint32_t lastChangeUs = micros();
  uint32_t startMs = millis();
  uint32_t lastBtnPollMs = millis();
  bool    anyEdgeSeen = false;

  while (sampleCount < MAX_SAMPLES) {
    int level = digitalRead(CC1101_GDO0_PIN);
    if (level != prevLevel) {
      uint32_t now = micros();
      uint32_t dur = now - lastChangeUs;
      if (dur > MAX_SAMPLE_US) dur = MAX_SAMPLE_US;
      // Sign carries the previous level — that's the level we just left.
      sampleBuf[sampleCount++] = prevLevel ? (int)dur : -(int)dur;
      lastChangeUs = now;
      prevLevel = level;
      anyEdgeSeen = true;
    }

    uint32_t nowUs = micros();
    if (anyEdgeSeen && (nowUs - lastChangeUs) > SILENCE_TIMEOUT_US) break;
    if (!anyEdgeSeen && (millis() - startMs) > LISTEN_TIMEOUT_MS) break;

    // Cheap-but-not-free SELECT poll. PCF8574 read is ~50us via I2C, so
    // do it at most every 20ms — the user's finger is slow.
    if (millis() - lastBtnPollMs > 20) {
      if (!pcf.digitalRead(BTN_SELECT)) break;
      lastBtnPollMs = millis();
    }
  }
  return sampleCount > 0;
}

// ============================================================
// SD write — Flipper-format .sub
// ============================================================
static bool writeSubFile(const char *path, uint32_t freqHz) {
  // VSPI re-pin in case a prior feature left it elsewhere — same pattern
  // as SubReplay::subReplaySetup().
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
  delay(5);
  SPI.begin(18, 19, 23, 5);
  if (!SD.begin(5)) { strncpy(errorMsg, "SD.begin failed", sizeof(errorMsg)); return false; }
  if (!ensureRecDir()) { strncpy(errorMsg, "mkdir failed",  sizeof(errorMsg)); return false; }

  File f = SD.open(path, FILE_WRITE);
  if (!f) { strncpy(errorMsg, "open(w) failed", sizeof(errorMsg)); return false; }

  f.println("Filetype: Flipper SubGhz RAW File");
  f.println("Version: 1");
  f.printf("Frequency: %lu\n", (unsigned long)freqHz);
  f.println("Preset: FuriHalSubGhzPresetOok650Async");
  f.println("Protocol: RAW");

  // Extra Hydra-namespaced headers when rc-switch decoded the signal —
  // ignored by stock Flipper firmware (it only parses Frequency/Preset/
  // Protocol/RAW_Data), but harmless to it and useful for our own
  // tooling that wants to skip the bit-bang path and re-send via
  // RCSwitch.send(value, bitLength) instead.
  if (decodedProtocol > 0) {
    f.printf("# Hydra_RCSwitch_Protocol: %d\n", decodedProtocol);
    f.printf("# Hydra_RCSwitch_BitLength: %d\n", decodedBitLength);
    f.printf("# Hydra_RCSwitch_Value: %lu\n", (unsigned long)decodedValue);
  }

  // Flipper convention: at most ~512 samples per RAW_Data: line; emit
  // continuation lines as needed. Saves us building a giant single line
  // that might exceed the parser's buffer on some implementations.
  const int CHUNK = 512;
  for (int i = 0; i < sampleCount; i += CHUNK) {
    f.print("RAW_Data:");
    int end = i + CHUNK;
    if (end > sampleCount) end = sampleCount;
    for (int j = i; j < end; j++) {
      f.print(' ');
      f.print(sampleBuf[j]);
    }
    f.println();
  }
  f.flush();
  f.close();
  return true;
}

// ============================================================
// Drawing
// ============================================================
static void drawHeader(const char *title) {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print(title);
}

static void drawPickList() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  if (activeCount == 0) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 110);
    tft.print("No channels available.");
    return;
  }

  if (cursor < viewOffset) viewOffset = cursor;
  if (cursor >= viewOffset + LIST_VISIBLE_ROWS)
    viewOffset = cursor - LIST_VISIBLE_ROWS + 1;

  int shown = activeCount - viewOffset;
  if (shown > LIST_VISIBLE_ROWS) shown = LIST_VISIBLE_ROWS;

  for (int row = 0; row < shown; row++) {
    int idx = viewOffset + row;
    int chanIdx = activeIdx[idx];
    uint32_t hz = WardriveConfig::channels[chanIdx].hz;
    int y = LIST_TOP_Y + row * LIST_ROW_HEIGHT;
    bool selected = (idx == cursor);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                     selected ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf(" %2d. %7.3f MHz ", idx + 1, hz / 1000000.0f);
  }

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(2, tft.height() - 30);
  tft.print("UP/DOWN pick  RIGHT start  SEL exit");
}

static void drawListening(uint32_t freqHz) {
  tft.fillRect(0, 60, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 90);
  tft.printf("Listening...");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 130);
  tft.printf("Freq: %.3f MHz", freqHz / 1000000.0f);

  tft.setCursor(10, 160);
  tft.print("Trigger your transmitter");
  tft.setCursor(10, 180);
  tft.print("near the device.");

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, tft.height() - 30);
  tft.print("SEL = abort");
}

static void drawDone(uint32_t freqHz) {
  tft.fillRect(0, 60, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, 80);
  tft.print("Captured");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 110);
  tft.printf("Samples: %d / %d", sampleCount, MAX_SAMPLES);
  tft.setCursor(10, 130);
  tft.printf("Freq:    %.3f MHz", freqHz / 1000000.0f);

  // If rc-switch recognised the signal, surface the decode in cyan so
  // the user knows it's something Replay can resend as a clean burst,
  // not just raw timing samples.
  if (decodedProtocol > 0) {
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(10, 150);
    tft.printf("%s", protocolName(decodedProtocol));
    tft.setCursor(10, 168);
    tft.printf("Code: 0x%lX  (%d bit)",
               (unsigned long)decodedValue, decodedBitLength);
  } else {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(10, 150);
    tft.print("Protocol: RAW only");
  }

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, 195);
  tft.print("UP    Save to SD");
  tft.setCursor(10, 210);
  tft.print("DOWN  Discard and re-listen");
  tft.setCursor(10, 225);
  tft.print("SEL   Exit");
}

static void drawSaved() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, 80);
  tft.print("Saved.");

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 120);
  tft.print(savedName);

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 170);
  tft.print("Press any button to re-listen.");
  tft.setCursor(10, 185);
  tft.print("SEL exits to SubGHz submenu.");
}

static void drawError() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.setCursor(10, 80);
  tft.print("Error");

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 120);
  tft.print(errorMsg);

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 180);
  tft.print("Press any button.");
}

// ============================================================
// PUBLIC API
// ============================================================
void subRecordSetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  WardriveConfig::ensureInit();
  rebuildActive();

  // Bias the initial cursor to 433.92 MHz if it's in the active set,
  // since that's where the user's first test transmitter almost always
  // lives.
  for (int i = 0; i < activeCount; i++) {
    if (WardriveConfig::channels[activeIdx[i]].hz == 433920000UL) {
      cursor = i;
      break;
    }
  }

  phase = PHASE_PICK;
  toggleHeldFromPrev = true;
  sampleCount = 0;
  chosenChannelIdx = -1;
  errorMsg[0] = '\0';
  savedName[0] = '\0';

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader("Record .sub  [pick freq]");
  drawPickList();
}

void subRecordLoop() {
  uint32_t now = millis();
  bool upPressed    = !pcf.digitalRead(BTN_UP);
  bool downPressed  = !pcf.digitalRead(BTN_DOWN);
  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool any = upPressed || downPressed || leftPressed || rightPressed;

  // Wait-for-release latch — same edge-detection pattern as SubReplay.
  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
    delay(15);
    return;
  }

  if (phase == PHASE_PICK) {
    bool redraw = false;
    if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      cursor = (cursor + 1) % activeCount;
      lastBtnMs = now;
      redraw = true;
    } else if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      cursor = (cursor - 1 + activeCount) % activeCount;
      lastBtnMs = now;
      redraw = true;
    } else if (rightPressed) {
      chosenChannelIdx = activeIdx[cursor];
      phase = PHASE_LISTEN;
      toggleHeldFromPrev = true;
      uint32_t freqHz = WardriveConfig::channels[chosenChannelIdx].hz;
      drawHeader("Record .sub  [listening]");
      drawListening(freqHz);

      // Now do the (blocking) capture. UI updates between phases.
      cc1101RxPrep(freqHz);
      bool got = captureSamples();
      cc1101RxDone();

      listenStartMs = now;
      if (got) {
        phase = PHASE_DONE;
        drawHeader("Record .sub  [captured]");
        drawDone(freqHz);
      } else {
        strncpy(errorMsg, "No signal detected (timeout).", sizeof(errorMsg));
        phase = PHASE_ERROR;
        drawHeader("Record .sub  [no signal]");
        drawError();
      }
      return;
    }
    if (redraw) drawPickList();
  }
  else if (phase == PHASE_DONE) {
    uint32_t freqHz = WardriveConfig::channels[chosenChannelIdx].hz;
    if (upPressed) {
      char path[80];
      nextRecFilename(path, sizeof(path));
      if (writeSubFile(path, freqHz)) {
        strncpy(savedName, path, sizeof(savedName) - 1);
        savedName[sizeof(savedName) - 1] = '\0';
        phase = PHASE_SAVED;
        toggleHeldFromPrev = true;
        drawHeader("Record .sub  [saved]");
        drawSaved();
      } else {
        phase = PHASE_ERROR;
        toggleHeldFromPrev = true;
        drawHeader("Record .sub  [save failed]");
        drawError();
      }
      return;
    }
    if (downPressed) {
      // Discard + re-listen on the same freq.
      phase = PHASE_LISTEN;
      toggleHeldFromPrev = true;
      drawHeader("Record .sub  [listening]");
      drawListening(freqHz);

      cc1101RxPrep(freqHz);
      bool got = captureSamples();
      cc1101RxDone();

      if (got) {
        phase = PHASE_DONE;
        drawHeader("Record .sub  [captured]");
        drawDone(freqHz);
      } else {
        strncpy(errorMsg, "No signal detected (timeout).", sizeof(errorMsg));
        phase = PHASE_ERROR;
        drawHeader("Record .sub  [no signal]");
        drawError();
      }
      return;
    }
  }
  else if (phase == PHASE_SAVED || phase == PHASE_ERROR) {
    if (any) {
      // Back to the freq picker so user can record another file.
      phase = PHASE_PICK;
      toggleHeldFromPrev = true;
      sampleCount = 0;
      errorMsg[0] = '\0';
      drawHeader("Record .sub  [pick freq]");
      drawPickList();
    }
  }

  delay(15);
}

}  // namespace SubRecord
