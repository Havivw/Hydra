/*
 * CC1101 Spectrum Analyzer — see header.
 */

#include "spectrum_analyzer.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"
#include "subconfig.h"
#include "shared.h"
#include "sub_shared.h"

namespace SpectrumAnalyzer {

// RSSI mapping: stronger = taller. Floor and ceiling tuned for sub-GHz
// noise floor / typical Tx levels.
static const int RSSI_FLOOR   = -110;
static const int RSSI_CEILING = -30;

// Display layout
static const int BAR_AREA_TOP    = 60;
static const int BAR_AREA_BOTTOM = 250;
static const int BAR_AREA_HEIGHT = BAR_AREA_BOTTOM - BAR_AREA_TOP;
static const int LABEL_Y         = 255;   // freq labels under bars

// Per-channel dwell when sampling. Shorter = faster refresh but more
// chance to miss a brief Tx burst.
static const uint32_t SAMPLE_DWELL_MS = 35;

// Last-rendered RSSI per channel — only redraw changed bars to reduce flicker
static int  lastRssi[HYDRA_WARDRIVE_CHANNEL_COUNT];
static bool hasLast = false;

// Active-channel indices (built each setup; just a snapshot of which entries
// in WardriveConfig::channels are currently selected)
static int  activeIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int  activeCount = 0;

static void rebuildActive() {
  activeCount = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (WardriveConfig::channels[i].selected) {
      activeIdx[activeCount++] = i;
    }
  }
}

static uint16_t colorForRssi(int rssi) {
  if (rssi >= -50) return TFT_RED;
  if (rssi >= -70) return TFT_ORANGE;
  if (rssi >= -85) return TFT_YELLOW;
  return TFT_GREEN;
}

static int barHeightFor(int rssi) {
  if (rssi <= RSSI_FLOOR)   return 0;
  if (rssi >= RSSI_CEILING) return BAR_AREA_HEIGHT;
  return (rssi - RSSI_FLOOR) * BAR_AREA_HEIGHT / (RSSI_CEILING - RSSI_FLOOR);
}

static void drawScale() {
  // y-axis indicator on the left edge
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(0, BAR_AREA_TOP - 2);
  tft.printf("%d", RSSI_CEILING);
  tft.setCursor(0, BAR_AREA_BOTTOM - 6);
  tft.printf("%d", RSSI_FLOOR);
  tft.setCursor(0, BAR_AREA_TOP + BAR_AREA_HEIGHT / 2);
  tft.print("dBm");
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Spectrum  %d ch  [SEL]=exit", activeCount);
}

static int barWidth() {
  if (activeCount <= 0) return 0;
  int w = (tft.width() - 24) / activeCount;
  if (w < 2) w = 2;
  return w;
}

static int barX(int slot) {
  return 22 + slot * barWidth();
}

static void drawBar(int slot, int rssi, int prevRssi) {
  int x = barX(slot);
  int w = barWidth() - 1;   // 1px gap between bars
  int newH = barHeightFor(rssi);
  int oldH = hasLast ? barHeightFor(prevRssi) : 0;

  // Clear above the new top (if previous was taller)
  if (oldH > newH) {
    tft.fillRect(x, BAR_AREA_BOTTOM - oldH, w, oldH - newH, TFT_BLACK);
  }
  // Draw the bar
  if (newH > 0) {
    tft.fillRect(x, BAR_AREA_BOTTOM - newH, w, newH, colorForRssi(rssi));
  }
}

static void drawFreqLabel(int slot, uint32_t hz) {
  // Show last 3 digits of MHz (e.g. 433.920 → "433")
  int x = barX(slot);
  int mhz_int = hz / 1000000;
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(x, LABEL_Y);
  tft.printf("%d", mhz_int);
}

void spectrumSetup() {
  subghzReleasePinsFromNrf();

  WardriveConfig::ensureInit();
  rebuildActive();
  hasLast = false;

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  cc1101InitForDivV1();

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawScale();

  // Draw a baseline + freq labels under bars
  tft.drawLine(20, BAR_AREA_BOTTOM, tft.width(), BAR_AREA_BOTTOM, TFT_DARKGREY);
  for (int s = 0; s < activeCount; s++) {
    drawFreqLabel(s, WardriveConfig::channels[activeIdx[s]].hz);
  }

  if (activeCount == 0) {
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, BAR_AREA_TOP + 30);
    tft.print("No channels selected!");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, BAR_AREA_TOP + 55);
    tft.print("Settings -> Wardrive Channels");
  }
}

void spectrumLoop() {
  if (activeCount == 0) {
    delay(100);
    return;
  }

  // One full sweep per loop call. Roughly (35ms * N) per refresh.
  for (int s = 0; s < activeCount; s++) {
    int chanIdx = activeIdx[s];
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(WardriveConfig::channels[chanIdx].hz / 1000000.0f);
    ELECHOUSE_cc1101.SetRx();
    delay(SAMPLE_DWELL_MS);
    int rssi = ELECHOUSE_cc1101.getRssi();
    drawBar(s, rssi, hasLast ? lastRssi[chanIdx] : RSSI_FLOOR);
    lastRssi[chanIdx] = rssi;
  }
  hasLast = true;
}

}  // namespace SpectrumAnalyzer
