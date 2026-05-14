/*
 * NRF24 2.4 GHz Wardrive — see header.
 */

#include "nrf_wardrive.h"
#include "gps.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

#define BTN_UP 6

#define NRF_R_REGISTER 0x00
#define NRF_W_REGISTER 0x20
#define NRF_REG_CONFIG 0x00
#define NRF_REG_EN_AA  0x01
#define NRF_REG_RF_CH  0x05
#define NRF_REG_RF_SET 0x06
#define NRF_REG_RPD    0x09

namespace NrfWardrive {

#define CHANNELS 126
static const uint32_t HOP_DWELL_MS = 100;
static const uint32_t LOG_COOLDOWN_MS = 2000;
static uint32_t lastLoggedAt[CHANNELS];

static int  curCh = 0;
static bool autoHop = true;
static uint32_t lastHopMs = 0;
static uint32_t lastBtnMs = 0;
static bool toggleHeldFromPrev = true;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static uint32_t lastUiMs = 0;
static uint32_t totalHits = 0;
static bool sdReady = false;
static File csvFile;

#define LIST_ROWS 8
#define ROW_HEIGHT 14
#define LIST_TOP_Y 110
struct DispRow { char text[44]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static byte nrfWriteRegister(byte r, byte v) {
  digitalWrite(HYDRA_NRF1_CSN, LOW);
  SPI.transfer((r & 0x1F) | NRF_W_REGISTER);
  byte ret = SPI.transfer(v);
  digitalWrite(HYDRA_NRF1_CSN, HIGH);
  return ret;
}
static byte nrfReadRegister(byte r) {
  digitalWrite(HYDRA_NRF1_CSN, LOW);
  SPI.transfer(r & 0x1F);
  byte c = SPI.transfer(0);
  digitalWrite(HYDRA_NRF1_CSN, HIGH);
  return c;
}
static bool carrierDetected() { return nrfReadRegister(NRF_REG_RPD) & 0x01; }

static void enableRx(uint8_t ch) {
  digitalWrite(HYDRA_NRF1_CE, LOW);
  nrfWriteRegister(NRF_REG_EN_AA, 0x00);
  nrfWriteRegister(NRF_REG_RF_SET, 0x0F);
  nrfWriteRegister(NRF_REG_RF_CH, ch);
  nrfWriteRegister(NRF_REG_CONFIG, 0x03);
  digitalWrite(HYDRA_NRF1_CE, HIGH);
  delayMicroseconds(150);
}

static bool openCsv() {
  if (!SD.begin(5)) return false;
  if (!SD.exists("/hydra")) SD.mkdir("/hydra");
  bool needHeader = !SD.exists("/hydra/nrf_wardrive.csv");
  csvFile = SD.open("/hydra/nrf_wardrive.csv", FILE_APPEND);
  if (!csvFile) return false;
  if (needHeader) {
    csvFile.println("utc_time,date,latitude,longitude,channel,freq_mhz,sats");
    csvFile.flush();
  }
  return true;
}

static void logHit(uint8_t ch) {
  if (!sdReady) return;
  bool fix = Gps::hasFix();
  csvFile.printf("%s,%s,%s,%s,%u,%u,%u\n",
                 Gps::timeStr(), Gps::dateStr(),
                 fix ? String(Gps::latitude(), 6).c_str() : "",
                 fix ? String(Gps::longitude(), 6).c_str() : "",
                 (unsigned)ch, (unsigned)(2400 + ch),
                 (unsigned)Gps::satCount());
  csvFile.flush();
}

static void pushDispRow(uint8_t ch) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& r = dispRows[dispRowCount++];
  bool fix = Gps::hasFix();
  if (fix) {
    snprintf(r.text, sizeof(r.text), "%s ch%-3u %uMHz",
             Gps::timeStr(), (unsigned)ch, (unsigned)(2400 + ch));
  } else {
    snprintf(r.text, sizeof(r.text), "(no-fix) ch%-3u %uMHz",
             (unsigned)ch, (unsigned)(2400 + ch));
  }
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 70, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("[!] NRF Wardrive");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 60);
  tft.print("File: /hydra/nrf_wardrive.csv");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 75);
  tft.print("[UP] toggle hop  [SEL] exit");
  tft.setCursor(2, 90);
  tft.printf("GPS: %s (sats=%d)",
             Gps::hasFix() ? "FIX" : "no fix", (int)Gps::satCount());
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("ch%-3d %uMHz  Hits:%u  %s",
             curCh, (unsigned)(2400 + curCh),
             (unsigned)totalHits, autoHop ? "HOP" : "STAY");
  tft.setCursor(175, 24);
  tft.setTextColor(sdReady ? TFT_GREEN : TFT_RED);
  tft.print(sdReady ? "SD OK" : "NO SD");
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

void nrfWardriveSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  Gps::begin();

  memset(lastLoggedAt, 0, sizeof(lastLoggedAt));
  curCh = 0; autoHop = true;
  totalHits = 0; dispRowCount = 0;
  toggleHeldFromPrev = true;

  sdReady = openCsv();   // SD first, then NRF SPI access
  pinMode(HYDRA_NRF1_CE, OUTPUT);
  pinMode(HYDRA_NRF1_CSN, OUTPUT);
  digitalWrite(HYDRA_NRF1_CE, LOW);
  digitalWrite(HYDRA_NRF1_CSN, HIGH);
  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  delay(10);
  enableRx(curCh);

  Serial.printf("[NrfWardrive] SD=%s\n", sdReady ? "ok" : "FAILED");
  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawHeader();
  drawTopStatus();
  lastHopMs = millis();
  lastUiMs = millis();
}

void nrfWardriveLoop() {
  Gps::update();
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  uint32_t now = millis();

  // UP toggle hop (with debounce/release latch)
  bool upPressed = !pcf.digitalRead(BTN_UP);
  if (toggleHeldFromPrev) { if (!upPressed) toggleHeldFromPrev = false; }
  else if (upPressed && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    autoHop = !autoHop;
    lastBtnMs = now;
    toggleHeldFromPrev = true;
  }

  // Sample carrier on current channel
  if (carrierDetected()) {
    if (now - lastLoggedAt[curCh] >= LOG_COOLDOWN_MS) {
      lastLoggedAt[curCh] = now;
      totalHits++;
      logHit(curCh);
      pushDispRow(curCh);
      redrawList();
      Serial.printf("[NrfWardrive] hit ch%d (%uMHz) fix=%d\n",
                    curCh, (unsigned)(2400 + curCh), Gps::hasFix());
    }
  }

  // Auto-hop
  if (autoHop && now - lastHopMs >= HOP_DWELL_MS) {
    curCh = (curCh + 1) % CHANNELS;
    enableRx(curCh);
    lastHopMs = now;
  }

  // UI refresh
  if (now - lastUiMs > 250) {
    drawTopStatus();
    drawHeader();
    lastUiMs = now;
  }
}

}  // namespace NrfWardrive
