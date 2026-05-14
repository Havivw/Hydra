/*
 * SD Format — see header.
 */

#include "sd_format.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5
// SELECT is intentionally handled by the main loop — pressing it exits.

namespace SdFormat {

enum Phase {
  PHASE_INIT       = 0,  // mounting SD
  PHASE_CONFIRM    = 1,  // ask for confirmation
  PHASE_WORKING    = 2,  // deleting
  PHASE_DONE       = 3,  // success
  PHASE_ERROR      = 4   // SD mount failed
};

static Phase phase = PHASE_INIT;
static uint32_t deletedFiles = 0;
static uint32_t deletedDirs = 0;
static uint32_t lastRedraw = 0;
static uint32_t lastNavMs = 0;
static const uint32_t NAV_DEBOUNCE_MS = 200;

// Mount SD on VSPI (SCK=18, MISO=19, MOSI=23, CS=5). The explicit SPI.begin
// re-pin matters when this is the first VSPI consumer in the session — it
// gets VSPI's pin matrix into a known state before the SD library's own
// internal SPI.begin() (which is a no-op once SPI has been initialised).
// Falls back to 4 MHz if the default clock fails (some older cards are picky).
static bool mountSD() {
  SPI.begin(18, 19, 23, 5);
  delay(10);
  if (!SD.begin(5)) {
    SD.end();
    delay(20);
    if (!SD.begin(5, SPI, 4000000)) return false;
  }
  return SD.cardType() != CARD_NONE;
}

static void purgeRecursive(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    String path = String(entry.path());
    bool isDir = entry.isDirectory();

    if (isDir) {
      purgeRecursive(entry);
      entry.close();
      if (SD.rmdir(path)) {
        deletedDirs++;
        Serial.printf("[SdFormat] rmdir %s\n", path.c_str());
      }
    } else {
      entry.close();
      if (SD.remove(path)) {
        deletedFiles++;
        Serial.printf("[SdFormat] rm %s\n", path.c_str());
      }
    }
  }
}

static void runPurge() {
  deletedFiles = 0;
  deletedDirs = 0;
  File root = SD.open("/");
  if (!root) {
    phase = PHASE_ERROR;
    return;
  }
  purgeRecursive(root);
  root.close();
  phase = PHASE_DONE;
}

static void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);

  if (phase == PHASE_INIT) {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 60);
    tft.print("[*] Mounting SD card...");
  }
  else if (phase == PHASE_CONFIRM) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 50);
    tft.print("[!] FORMAT SD CARD");

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 90);
    tft.print("This will DELETE every file");
    tft.setCursor(10, 110);
    tft.print("and folder on the SD card.");
    tft.setCursor(10, 130);
    tft.print("Cannot be undone.");

    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 180);
    tft.print("[RIGHT]  Confirm");
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 200);
    tft.print("[LEFT]   Cancel");
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(10, 220);
    tft.print("[SELECT] Exit");
  }
  else if (phase == PHASE_WORKING) {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 60);
    tft.print("[*] Formatting...");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 90);
    tft.printf("Files removed: %u", (unsigned)deletedFiles);
    tft.setCursor(10, 110);
    tft.printf("Dirs removed:  %u", (unsigned)deletedDirs);
  }
  else if (phase == PHASE_DONE) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 60);
    tft.print("[+] Format complete");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 100);
    tft.printf("Files removed: %u", (unsigned)deletedFiles);
    tft.setCursor(10, 120);
    tft.printf("Dirs removed:  %u", (unsigned)deletedDirs);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(10, 180);
    tft.print("[SELECT] Back to menu");
  }
  else {  // PHASE_ERROR
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 60);
    tft.print("[!] SD mount failed");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 100);
    tft.print("Check that the card is");
    tft.setCursor(10, 120);
    tft.print("inserted and not damaged.");
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(10, 180);
    tft.print("[SELECT] Back to menu");
  }

  float v = readBatteryVoltage();
  drawStatusBar(v, true);
}

void sdFormatSetup() {
  phase = PHASE_INIT;
  deletedFiles = 0;
  deletedDirs = 0;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  drawScreen();

  if (mountSD()) {
    phase = PHASE_CONFIRM;
  } else {
    phase = PHASE_ERROR;
  }
  drawScreen();
  lastRedraw = millis();
}

void sdFormatLoop() {
  uint32_t now = millis();

  if (phase == PHASE_CONFIRM) {
    if (now - lastNavMs > NAV_DEBOUNCE_MS) {
      if (!pcf.digitalRead(BTN_RIGHT)) {
        phase = PHASE_WORKING;
        drawScreen();
        lastNavMs = now;
        // Run synchronously — purge blocks; UI updates intermittently from
        // Serial logs. For large cards this might take a few seconds.
        runPurge();
        drawScreen();
      } else if (!pcf.digitalRead(BTN_LEFT)) {
        // Cancel: go straight to PHASE_DONE-style screen showing zero ops
        phase = PHASE_DONE;
        drawScreen();
        lastNavMs = now;
      }
    }
  }
  else if (phase == PHASE_WORKING) {
    if (now - lastRedraw > 400) {
      drawScreen();
      lastRedraw = now;
    }
  }
  delay(20);
}

}  // namespace SdFormat
