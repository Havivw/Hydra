/*
 * NRF24 Channel Heatmap — see header.
 */

#include "nrf_heatmap.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>

#define DARK_GRAY 0x4208

// NRF1 wiring on DIV v1
#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

// NRF24L01 register opcodes
#define NRF_R_REGISTER  0x00
#define NRF_W_REGISTER  0x20
#define NRF_REG_CONFIG  0x00
#define NRF_REG_EN_AA   0x01
#define NRF_REG_RF_CH   0x05
#define NRF_REG_RF_SET  0x06
#define NRF_REG_RPD     0x09

namespace NrfHeatmap {

#define CHANNELS 128

// Per-channel activity score 0..255. Each "hit" (RPD=1) bumps it up by
// ACTIVITY_BUMP; every second we decay by ACTIVITY_DECAY. Steady state on
// a channel with a 1 Hz transmitter ≈ ACTIVITY_BUMP/ACTIVITY_DECAY * 255.
static uint8_t activity[CHANNELS];

#define ACTIVITY_BUMP   60
#define ACTIVITY_DECAY  3
#define ACTIVITY_DECAY_PERIOD_MS 200

static uint32_t lastDecayMs = 0;
static uint32_t lastUiMs = 0;
static uint32_t totalCarriers = 0;
static int curChannel = 0;

// Raw NRF register access via SPI (cifertech Scanner pattern)
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

static bool carrierDetected() {
  return nrfReadRegister(NRF_REG_RPD) & 0x01;
}

static void enableRx(uint8_t channel) {
  // PWR_UP, PRIM_RX, 1Mbps, no auto-ack, no CRC
  digitalWrite(HYDRA_NRF1_CE, LOW);
  nrfWriteRegister(NRF_REG_EN_AA, 0x00);
  nrfWriteRegister(NRF_REG_RF_SET, 0x0F);
  nrfWriteRegister(NRF_REG_RF_CH, channel);
  nrfWriteRegister(NRF_REG_CONFIG, 0x03); // PWR_UP | PRIM_RX
  digitalWrite(HYDRA_NRF1_CE, HIGH);
  delayMicroseconds(150);
}

static uint16_t colorForActivity(uint8_t a) {
  if (a >= 200) return TFT_RED;
  if (a >= 140) return TFT_ORANGE;
  if (a >= 80)  return TFT_YELLOW;
  if (a >= 30)  return TFT_GREEN;
  return TFT_DARKGREY;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("NRF Heatmap  ch%d  carriers:%u  [SEL]=exit",
             curChannel, (unsigned)totalCarriers);
}

// Render: vertical bars across 128 channels. Each bar = 2 px wide × N tall.
#define BAR_TOP    65
#define BAR_BOTTOM 250
#define BAR_HEIGHT (BAR_BOTTOM - BAR_TOP)
#define BAR_PIXELS_PER_CH 1   // 128 ch × 1 px ≈ 128 px; leaves space for axis

static int barX(int ch) { return 20 + ch * BAR_PIXELS_PER_CH; }

static void drawBar(int ch, int prev, int cur) {
  int x = barX(ch);
  int oldH = (prev * BAR_HEIGHT) / 255;
  int newH = (cur  * BAR_HEIGHT) / 255;
  if (oldH > newH) {
    tft.fillRect(x, BAR_BOTTOM - oldH, BAR_PIXELS_PER_CH, oldH - newH, TFT_BLACK);
  }
  if (newH > 0) {
    tft.fillRect(x, BAR_BOTTOM - newH, BAR_PIXELS_PER_CH, newH, colorForActivity(cur));
  }
}

static uint8_t lastDrawn[CHANNELS];

static void drawScale() {
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawLine(20, BAR_BOTTOM, 20 + CHANNELS, BAR_BOTTOM, TFT_DARKGREY);
  tft.setCursor(20, BAR_BOTTOM + 4);
  tft.print("0");
  tft.setCursor(20 + 32, BAR_BOTTOM + 4);
  tft.print("32");
  tft.setCursor(20 + 64, BAR_BOTTOM + 4);
  tft.print("64");
  tft.setCursor(20 + 96, BAR_BOTTOM + 4);
  tft.print("96");
  tft.setCursor(20 + 124, BAR_BOTTOM + 4);
  tft.print("125");
  tft.setCursor(0, BAR_BOTTOM + 14);
  tft.print("ch (2400 + ch MHz)");
}

void heatmapSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  memset(activity, 0, CHANNELS);
  memset(lastDrawn, 0, CHANNELS);
  totalCarriers = 0;
  curChannel = 0;

  // Init SPI + NRF chip select pins
  pinMode(HYDRA_NRF1_CE, OUTPUT);
  pinMode(HYDRA_NRF1_CSN, OUTPUT);
  digitalWrite(HYDRA_NRF1_CE, LOW);
  digitalWrite(HYDRA_NRF1_CSN, HIGH);
  SPI.begin(18, 19, 23, 17);  // explicit VSPI pins; bare SPI.begin() inherits TFT_eSPI state
  delay(10);
  enableRx(0);

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawScale();
  lastDecayMs = millis();
  lastUiMs = millis();
}

void heatmapLoop() {
  uint32_t now = millis();

  // Hop one channel per loop iteration, sample carrier
  enableRx(curChannel);
  delayMicroseconds(140);
  if (carrierDetected()) {
    int v = (int)activity[curChannel] + ACTIVITY_BUMP;
    if (v > 255) v = 255;
    activity[curChannel] = (uint8_t)v;
    totalCarriers++;
  }

  curChannel = (curChannel + 1) % CHANNELS;

  // Periodic decay across all channels
  if (now - lastDecayMs >= ACTIVITY_DECAY_PERIOD_MS) {
    for (int i = 0; i < CHANNELS; i++) {
      if (activity[i] >= ACTIVITY_DECAY) activity[i] -= ACTIVITY_DECAY;
      else activity[i] = 0;
    }
    lastDecayMs = now;
  }

  // Redraw bars at ~5 Hz — incremental, only changed ones
  if (now - lastUiMs > 200) {
    for (int i = 0; i < CHANNELS; i++) {
      if (activity[i] != lastDrawn[i]) {
        drawBar(i, lastDrawn[i], activity[i]);
        lastDrawn[i] = activity[i];
      }
    }
    drawHeader();
    lastUiMs = now;
  }
}

}  // namespace NrfHeatmap
