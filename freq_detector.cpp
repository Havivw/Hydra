/*
 * CC1101 Frequency Detector — see header.
 */

#include "freq_detector.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"
#include "subconfig.h"
#include "shared.h"
#include "sub_shared.h"

namespace FreqDetector {

// Per-channel state: rolling peak RSSI + decay timestamp so a brief loud
// burst stays on the list for a few seconds before falling off.
struct ChanState {
  int8_t   peakRssi;
  uint32_t peakAt;
};
static ChanState chans[HYDRA_WARDRIVE_CHANNEL_COUNT];

static int  activeIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int  activeCount = 0;
static int  curSlot = 0;

static const uint32_t SAMPLE_DWELL_MS = 30;
static const uint32_t PEAK_DECAY_MS   = 3000;   // peak fades after this
static const uint32_t REDRAW_PERIOD   = 250;

static uint32_t lastRedrawMs = 0;

static void rebuildActive() {
  activeCount = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (WardriveConfig::channels[i].selected) activeIdx[activeCount++] = i;
  }
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    chans[i].peakRssi = -127;
    chans[i].peakAt = 0;
  }
  curSlot = 0;
}

static uint16_t colorForRssi(int rssi) {
  if (rssi >= -50) return TFT_RED;
  if (rssi >= -70) return TFT_ORANGE;
  if (rssi >= -85) return TFT_YELLOW;
  return TFT_GREEN;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Freq Detector  %d ch  [SEL]=exit", activeCount);
}

// Render the top-N strongest channels (where N fits the screen). Simple
// selection sort over a small list, called at ~4 Hz.
#define TOP_N 12
struct Row { int chanIdx; int rssi; };
static void drawTable() {
  // Collect non-expired peaks
  Row rows[HYDRA_WARDRIVE_CHANNEL_COUNT];
  int n = 0;
  uint32_t now = millis();
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (!WardriveConfig::channels[i].selected) continue;
    if (chans[i].peakRssi <= -127) continue;
    if (now - chans[i].peakAt > PEAK_DECAY_MS) continue;
    rows[n++] = { i, chans[i].peakRssi };
  }
  // Sort descending by RSSI (simple selection-sort, n is small)
  for (int i = 0; i < n; i++) {
    int max = i;
    for (int j = i + 1; j < n; j++) if (rows[j].rssi > rows[max].rssi) max = j;
    Row tmp = rows[i]; rows[i] = rows[max]; rows[max] = tmp;
  }

  // Clear and redraw table area
  tft.fillRect(0, 65, tft.width(), tft.height() - 70, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(2, 70);
  tft.print("# Freq          dBm  age");

  if (n == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 90);
    tft.print("(no signals above noise yet)");
    return;
  }

  int rowsToShow = n > TOP_N ? TOP_N : n;
  for (int r = 0; r < rowsToShow; r++) {
    uint32_t hz = WardriveConfig::channels[rows[r].chanIdx].hz;
    uint32_t age = now - chans[rows[r].chanIdx].peakAt;
    int y = 90 + r * 14;
    tft.setTextColor(colorForRssi(rows[r].rssi), TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf("%d %7.3fMHz   %4d  %us",
               r + 1, hz / 1000000.0f, rows[r].rssi, (unsigned)(age / 1000));
  }

  // Headline strongest at top
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(colorForRssi(rows[0].rssi), TFT_BLACK);
  tft.setCursor(2, tft.height() - 26);
  uint32_t hz0 = WardriveConfig::channels[rows[0].chanIdx].hz;
  tft.printf("STRONGEST: %.3f MHz @ %d dBm",
             hz0 / 1000000.0f, rows[0].rssi);
}

void freqDetectorSetup() {
  subghzReleasePinsFromNrf();

  WardriveConfig::ensureInit();
  rebuildActive();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  cc1101InitForDivV1();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawTable();
  lastRedrawMs = millis();
}

void freqDetectorLoop() {
  if (activeCount == 0) {
    if (millis() - lastRedrawMs > 500) {
      tft.fillRect(0, 65, tft.width(), tft.height() - 70, TFT_BLACK);
      tft.setTextFont(2);
      tft.setTextSize(1);
      tft.setTextColor(TFT_RED);
      tft.setCursor(10, 110);
      tft.print("No channels selected!");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(10, 135);
      tft.print("Settings -> Wardrive Channels");
      lastRedrawMs = millis();
    }
    delay(50);
    return;
  }

  // Sample one channel per loop call — the loop is high-frequency
  int chanIdx = activeIdx[curSlot];
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(WardriveConfig::channels[chanIdx].hz / 1000000.0f);
  ELECHOUSE_cc1101.SetRx();
  delay(SAMPLE_DWELL_MS);
  int rssi = ELECHOUSE_cc1101.getRssi();

  // Update rolling peak with decay
  uint32_t now = millis();
  bool expired = (now - chans[chanIdx].peakAt) > PEAK_DECAY_MS;
  if (expired || rssi > chans[chanIdx].peakRssi) {
    chans[chanIdx].peakRssi = (int8_t)rssi;
    chans[chanIdx].peakAt = now;
  }

  curSlot = (curSlot + 1) % activeCount;

  if (now - lastRedrawMs >= REDRAW_PERIOD) {
    drawTable();
    lastRedrawMs = now;
  }
}

}  // namespace FreqDetector
