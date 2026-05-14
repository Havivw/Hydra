/*
 * GPS Status — see header.
 */

#include "gps_status.h"
#include "gps.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#define DARK_GRAY 0x4208

namespace GpsStatus {

static uint32_t lastRedraw = 0;
static uint32_t lastSentenceCount = 0;
static uint32_t lastRateSnapshot = 0;
static float    sentencesPerSec = 0.0f;

static void drawScreen() {
  tft.fillRect(0, 40, tft.width(), tft.height() - 40, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, 50);
  tft.print("[*] GPS Status");

  tft.setTextFont(1);
  tft.setTextSize(1);

  // Fix state — coloured header
  bool fix = Gps::hasFix();
  uint32_t sentences = Gps::sentenceCount();

  tft.setCursor(10, 90);
  tft.setTextColor(fix ? TFT_GREEN : (sentences > 0 ? TFT_YELLOW : TFT_RED));
  if (fix) {
    tft.print("FIX:       YES");
  } else if (sentences > 0) {
    tft.print("FIX:       searching");
  } else {
    tft.print("FIX:       no data");
  }

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 110);
  tft.printf("Sats:      %d", (int)Gps::satCount());

  tft.setCursor(10, 130);
  if (fix) {
    tft.printf("Lat:       %.6f", Gps::latitude());
  } else {
    tft.print("Lat:       ---");
  }

  tft.setCursor(10, 150);
  if (fix) {
    tft.printf("Lon:       %.6f", Gps::longitude());
  } else {
    tft.print("Lon:       ---");
  }

  tft.setCursor(10, 170);
  tft.printf("Time:      %s UTC", Gps::timeStr());

  tft.setCursor(10, 190);
  tft.printf("Date:      %s", Gps::dateStr());

  tft.setCursor(10, 220);
  tft.setTextColor(TFT_CYAN);
  tft.printf("NMEA sentences: %u (%.1f/s)",
             (unsigned)sentences, sentencesPerSec);

  tft.setCursor(10, 240);
  tft.setTextColor(TFT_DARKGREY);
  tft.printf("UART2: RX=GPIO %d  TX=GPIO %d  %d baud",
             HYDRA_GPS_UART_RX, HYDRA_GPS_UART_TX, HYDRA_GPS_BAUD);

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 270);
  tft.print("[SELECT] Exit");
}

void gpsStatusSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  Gps::begin();
  lastSentenceCount = Gps::sentenceCount();
  lastRateSnapshot  = millis();
  sentencesPerSec   = 0.0f;

  float v = readBatteryVoltage();
  drawStatusBar(v, false);

  drawScreen();
  lastRedraw = millis();
}

void gpsStatusLoop() {
  Gps::update();

  uint32_t now = millis();

  // Rolling sentence-rate window every 1 sec
  if (now - lastRateSnapshot >= 1000) {
    uint32_t newCount = Gps::sentenceCount();
    uint32_t delta = newCount - lastSentenceCount;
    float dtSec = (now - lastRateSnapshot) / 1000.0f;
    sentencesPerSec = dtSec > 0 ? delta / dtSec : 0;
    lastSentenceCount = newCount;
    lastRateSnapshot = now;
  }

  // Redraw at ~5 Hz so the time field doesn't lag
  if (now - lastRedraw >= 200) {
    drawScreen();
    lastRedraw = now;
  }

  delay(20);
}

}  // namespace GpsStatus
