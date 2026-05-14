/*
 * SubGHz Wardrive — see header.
 */

#include "subghz_wardrive.h"
#include "gps.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include "subconfig.h"   // brings in ELECHOUSE_CC1101_ESP32DIV — the DIV v1 CC1101 driver fork
#include "shared.h"
#include "sub_shared.h"

namespace SubGhzWardrive {

// Channels are owned by WardriveConfig — Settings → Wardrive Channels lets
// the user toggle which subset is active. We walk the full table and skip
// any entries that aren't currently selected.

// RSSI cutoff for "this is a real transmission, not noise". CC1101's noise
// floor on these bands is around -95 to -100 dBm; deliberate Tx from
// nearby devices typically lands above -75.
static const int RSSI_THRESHOLD = -75;

// Dwell time per frequency in auto-hop mode.
static const uint32_t HOP_DWELL_MS = 1500;

// Per-channel dedupe: don't log the same hit on the same channel more than
// once per N ms. Keeps the CSV usable when a stuck transmitter is
// broadcasting continuously.
static const uint32_t FREQ_LOG_COOLDOWN_MS = 1500;
static uint32_t lastLoggedAt[HYDRA_WARDRIVE_CHANNEL_COUNT] = {0};

static int      curFreqIdx = 0;
static bool     autoHop = true;
static uint32_t lastHopMs = 0;
static uint32_t totalHits = 0;
static uint32_t lastUiUpdate = 0;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool upHeldFromPrev = true;

static File csvFile;
static bool sdReady = false;

// Display state
#define LIST_ROWS 10
#define ROW_HEIGHT 14
#define LIST_TOP_Y 110
struct DispRow { char text[44]; };
static DispRow dispRows[LIST_ROWS];
static int dispRowCount = 0;

static void setRxFrequency(int idx) {
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(WardriveConfig::channels[idx].hz / 1000000.0f);
  ELECHOUSE_cc1101.SetRx();
}

// Find the next selected channel from `from` (inclusive) wrapping around.
// Returns -1 if nothing is selected.
static int nextSelectedFrom(int from) {
  for (int step = 0; step < HYDRA_WARDRIVE_CHANNEL_COUNT; step++) {
    int idx = (from + step) % HYDRA_WARDRIVE_CHANNEL_COUNT;
    if (WardriveConfig::channels[idx].selected) return idx;
  }
  return -1;
}

static bool openCsv() {
  // SD.begin idempotent. If our boot-time probe failed and a card has been
  // inserted since, this gives us a fresh chance to mount.
  if (!SD.begin(5)) return false;
  if (!SD.exists("/hydra")) SD.mkdir("/hydra");

  bool needHeader = !SD.exists("/hydra/sub_wardrive.csv");
  csvFile = SD.open("/hydra/sub_wardrive.csv", FILE_APPEND);
  if (!csvFile) return false;
  if (needHeader) {
    csvFile.println("utc_time,date,latitude,longitude,freq_hz,rssi_dbm,sats");
    csvFile.flush();
  }
  return true;
}

static void logHit(uint32_t freqHz, int rssi) {
  if (!sdReady) return;
  bool fix = Gps::hasFix();
  csvFile.printf("%s,%s,%s,%s,%lu,%d,%u\n",
                 Gps::timeStr(),
                 Gps::dateStr(),
                 fix ? String(Gps::latitude(), 6).c_str() : "",
                 fix ? String(Gps::longitude(), 6).c_str() : "",
                 (unsigned long)freqHz,
                 rssi,
                 (unsigned)Gps::satCount());
  csvFile.flush();
}

static void pushDispRow(uint32_t freqHz, int rssi) {
  if (dispRowCount == LIST_ROWS) {
    for (int i = 1; i < LIST_ROWS; i++) dispRows[i - 1] = dispRows[i];
    dispRowCount--;
  }
  DispRow& row = dispRows[dispRowCount++];
  bool fix = Gps::hasFix();
  if (fix) {
    snprintf(row.text, sizeof(row.text), "%-9s %.3fMHz %ddBm",
             Gps::timeStr(), freqHz / 1000000.0f, rssi);
  } else {
    snprintf(row.text, sizeof(row.text), "(no-fix)  %.3fMHz %ddBm",
             freqHz / 1000000.0f, rssi);
  }
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("%.3fMHz  Hits:%u  %s",
             WardriveConfig::channels[curFreqIdx].hz / 1000000.0f,
             (unsigned)totalHits,
             autoHop ? "HOP" : "STAY");
  tft.setCursor(175, 24);
  tft.setTextColor(sdReady ? TFT_GREEN : TFT_RED);
  tft.print(sdReady ? "SD OK" : "NO SD");
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 60, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("[!] SubGHz Wardrive  %d ch active",
             WardriveConfig::selectedCount());
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 60);
  tft.printf("File: /hydra/sub_wardrive.csv");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 75);
  tft.print("[UP] toggle hop  [SELECT] exit");
  tft.setCursor(2, 90);
  tft.printf("GPS: %s  (sats=%d)",
             Gps::hasFix() ? "FIX" : "no fix", (int)Gps::satCount());
}

static void redrawList() {
  tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  for (int i = 0; i < dispRowCount; i++) {
    tft.setCursor(2, LIST_TOP_Y + i * ROW_HEIGHT);
    tft.print(dispRows[i].text);
  }
}

void wardriveSetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  WardriveConfig::ensureInit();

  autoHop = true;
  totalHits = 0;
  dispRowCount = 0;
  upHeldFromPrev = true;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) lastLoggedAt[i] = 0;

  // Start on the first selected channel. If nothing is selected the user is
  // shown a banner and the loop is a no-op until they fix it.
  curFreqIdx = nextSelectedFrom(0);
  if (curFreqIdx < 0) curFreqIdx = 0;

  pcf.pinMode(BTN_UP, INPUT_PULLUP);

  Gps::begin();  // safe to call repeatedly. NOTE: this permanently kills
                 // touch for the session — GPS UART2 sits on the same GPIOs
                 // as the XPT2046 CLK/MOSI. Wardrive is button-only by
                 // design; documented in Hydra/README.md.

  // ORDER MATTERS: mount SD BEFORE CC1101.Init(). CC1101 CSN is GPIO 5 —
  // the same physical pin as the SD card CS — so once the CC1101 driver
  // is initialised it's driving that pin. SD must claim it first so its
  // mount state is correct; both libraries cooperate on the shared CS via
  // active-low transactions once both are up.
  sdReady = openCsv();

  cc1101InitForDivV1();
  setRxFrequency(curFreqIdx);

  Serial.printf("[Wardrive] SD=%s  CC1101 RX@%.2fMHz  %d ch active\n",
                sdReady ? "ok" : "FAILED",
                WardriveConfig::channels[curFreqIdx].hz / 1000000.0f,
                WardriveConfig::selectedCount());

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);

  drawHeader();
  drawTopStatus();
  lastUiUpdate = millis();
  lastHopMs = millis();
}

void wardriveLoop() {
  Gps::update();
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);

  uint32_t now = millis();

  // UP toggles hop on/off. Edge-latched: must release UP before another
  // toggle can fire. Without this latch, holding UP flipped autoHop every
  // BTN_DEBOUNCE_MS forever.
  bool upPressed = !pcf.digitalRead(BTN_UP);
  if (upHeldFromPrev) {
    if (!upPressed) upHeldFromPrev = false;
  } else if (upPressed && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    autoHop = !autoHop;
    lastBtnMs = now;
    upHeldFromPrev = true;
    Serial.printf("[Wardrive] autoHop=%s\n", autoHop ? "ON" : "OFF");
  }

  // Skip the rest if nothing is selected — RSSI from an unconfigured radio
  // and freq hopping are both pointless.
  if (WardriveConfig::selectedCount() == 0) {
    if (now - lastUiUpdate > 500) {
      tft.fillRect(0, LIST_TOP_Y, tft.width(), LIST_ROWS * ROW_HEIGHT, TFT_BLACK);
      tft.setTextFont(2);
      tft.setTextSize(1);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(2, LIST_TOP_Y + 10);
      tft.print("No channels selected!");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(2, LIST_TOP_Y + 30);
      tft.print("Settings -> Wardrive Channels");
      lastUiUpdate = now;
    }
    delay(50);
    return;
  }

  // RSSI sample at current frequency
  int rssi = ELECHOUSE_cc1101.getRssi();
  if (rssi > RSSI_THRESHOLD) {
    if (now - lastLoggedAt[curFreqIdx] >= FREQ_LOG_COOLDOWN_MS) {
      lastLoggedAt[curFreqIdx] = now;
      totalHits++;
      uint32_t fHz = WardriveConfig::channels[curFreqIdx].hz;
      logHit(fHz, rssi);
      pushDispRow(fHz, rssi);
      redrawList();
      Serial.printf("[Wardrive] hit %.3fMHz rssi=%d fix=%d\n",
                    fHz / 1000000.0f, rssi, Gps::hasFix());
    }
  }

  // Channel hop — advance to the next *selected* channel
  if (autoHop && (now - lastHopMs >= HOP_DWELL_MS)) {
    int next = nextSelectedFrom(curFreqIdx + 1);
    if (next >= 0 && next != curFreqIdx) {
      curFreqIdx = next;
      setRxFrequency(curFreqIdx);
    }
    lastHopMs = now;
  }

  // Refresh top status + header at ~3 Hz so GPS sat count / freq update
  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    drawHeader();
    lastUiUpdate = now;
  }

  delay(15);
}

}  // namespace SubGhzWardrive
