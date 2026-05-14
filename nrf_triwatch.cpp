/*
 * Tri-Channel Watch — see header.
 *
 * Originally tried to use all 3 NRF24 modules simultaneously, but the
 * board's shared SPI / CSN wiring (NRF1 CE = CC1101 GDO2 = GPIO 16;
 * NRF2 CSN = CC1101 CSN = GPIO 27; NRF3 CE = TFT backlight = GPIO 4)
 * made reliable multi-radio operation impossible in software. Different
 * boots ended up with different radios working.
 *
 * Refactored: use NRF1 only (the most reliable on this board after the
 * CC1101-GDO-to-Hi-Z trick), round-robin it across 3 user-pickable
 * channels every CHANNEL_DWELL_MS. UI unchanged — three panels with
 * per-channel activity bars — but the underlying sampling is sequential
 * rather than parallel. Net result: 3-channel coverage that actually
 * works on this hardware.
 */

#include "nrf_triwatch.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SPI.h>
#include <RF24.h>

#define DARK_GRAY 0x4208

#define HYDRA_NRF1_CE  16
#define HYDRA_NRF1_CSN 17

#define BTN_UP    6
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 5

namespace NrfTriWatch {

static RF24 radio(HYDRA_NRF1_CE, HYDRA_NRF1_CSN, 16000000);
static bool radioOk = false;

static uint8_t chans[3] = { 12, 37, 76 };
static int    selectedSlot = 0;   // which slot the L/R adjusts
static int    activeSlot   = 0;   // which channel the radio is currently camped on

struct SlotState {
  uint16_t hits;
  uint32_t lastHitAt;
  uint8_t  intensity;
};
static SlotState slots[3];

#define INTENSITY_BUMP 50
#define INTENSITY_DECAY 4
#define INTENSITY_DECAY_PERIOD_MS 200
#define CHANNEL_DWELL_MS 400

static uint32_t lastDecayMs = 0;
static uint32_t lastHopMs = 0;
static uint32_t lastUiMs = 0;
static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;

static void tuneTo(uint8_t ch) {
  radio.stopListening();
  radio.setChannel(ch);
  radio.startListening();
}

static void armRadio() {
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_LOW, true);
  radio.disableCRC();
  radio.setAddressWidth(2);
  uint8_t addr[2] = { 0xAA, 0x00 };
  radio.openReadingPipe(0, addr);
  radio.setChannel(chans[activeSlot]);
  radio.setPayloadSize(32);
  radio.startListening();
}

static uint16_t colorForIntensity(uint8_t v) {
  if (v >= 200) return TFT_RED;
  if (v >= 130) return TFT_ORANGE;
  if (v >= 60)  return TFT_YELLOW;
  if (v >= 15)  return TFT_GREEN;
  return TFT_DARKGREY;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("Tri-Channel Watch  (radio %s)",
             radioOk ? "OK" : "FAIL");
}

static void drawSlot(int slot) {
  int y = 70 + slot * 70;
  tft.fillRect(0, y, tft.width(), 65, TFT_BLACK);

  bool sel = (slot == selectedSlot);
  bool live = (slot == activeSlot);
  uint16_t fg = sel ? TFT_ORANGE : TFT_WHITE;

  tft.setTextFont(2); tft.setTextSize(1);
  tft.setTextColor(fg);
  tft.setCursor(2, y);
  tft.printf("%s Slot %d  ch%d  (%u MHz)%s",
             sel ? ">" : " ",
             slot + 1,
             (int)chans[slot],
             (unsigned)(2400 + chans[slot]),
             live ? "  *live*" : "");

  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, y + 20);
  tft.printf("hits=%u", (unsigned)slots[slot].hits);

  // Intensity bar
  int barW = (slots[slot].intensity * 220) / 255;
  tft.fillRect(2, y + 35, 220, 18, TFT_BLACK);
  tft.drawRect(2, y + 35, 220, 18, TFT_DARKGREY);
  if (barW > 0) tft.fillRect(2, y + 35, barW, 18, colorForIntensity(slots[slot].intensity));
}

static void drawAll() {
  drawHeader();
  for (int i = 0; i < 3; i++) drawSlot(i);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, tft.height() - 16);
  tft.print("[U/D] slot  [L/R] channel  [SEL] exit");
}

void triWatchSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  selectedSlot = 0;
  activeSlot = 0;
  toggleHeldFromPrev = true;
  for (int i = 0; i < 3; i++) {
    slots[i].hits = 0;
    slots[i].intensity = 0;
    slots[i].lastHitAt = 0;
  }

  // Force VSPI onto the right pin map (TFT_eSPI claims HSPI at boot;
  // bare SPI.begin() inherits its leftovers and breaks the NRF24s).
  SPI.begin(18, 19, 23, 17);

  // CC1101 IOCFG -> Hi-Z so it stops driving GPIO 16 (NRF1 CE). The bytes
  // are sent over GPIO 27 (CC1101 CSN, shared with NRF2 CSN); NRF2 sees
  // junk register writes that don't matter since we don't use NRF2 here.
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  delayMicroseconds(100);
  digitalWrite(27, LOW);
  delayMicroseconds(20);
  SPI.transfer(0x00);  // W_REG IOCFG2
  SPI.transfer(0x2E);  // GDO2 -> Hi-Z, frees GPIO 16
  digitalWrite(27, HIGH);
  delayMicroseconds(50);

  // Cleanup-on-re-entry latch — on re-entry the radio was left listening,
  // so begin() on a running chip silently misconfigures it. Force CE/CSN
  // to idle + powerDown before re-init. Skipped on first entry.
  static bool hasBeenHere = false;
  if (hasBeenHere) {
    pinMode(16, OUTPUT); digitalWrite(16, LOW);
    pinMode(17, OUTPUT); digitalWrite(17, HIGH);
    delay(10);
    radio.powerDown();
    delay(10);
  }
  hasBeenHere = true;

  bool b = radio.begin();
  radioOk = b && radio.isChipConnected();

  Serial.printf("[TriWatch] NRF1 begin=%d connected=%d\n", b, radioOk);

  if (radioOk) {
    armRadio();
  }

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawAll();
  lastDecayMs = millis();
  lastHopMs = millis();
  lastUiMs = millis();
}

void triWatchLoop() {
  uint32_t now = millis();

  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (down)       selectedSlot = (selectedSlot + 1) % 3;
    else if (up)    selectedSlot = (selectedSlot + 2) % 3;
    else if (right) {
      chans[selectedSlot] = (chans[selectedSlot] + 1) % 126;
    } else if (left) {
      chans[selectedSlot] = (chans[selectedSlot] == 0) ? 125 : chans[selectedSlot] - 1;
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawAll();
  }

  if (!radioOk) {
    delay(50);
    return;
  }

  // Round-robin: every CHANNEL_DWELL_MS, hop to the next slot's channel
  if (now - lastHopMs >= CHANNEL_DWELL_MS) {
    activeSlot = (activeSlot + 1) % 3;
    tuneTo(chans[activeSlot]);
    lastHopMs = now;
  }

  // Drain anything received on the current channel into the active slot
  while (radio.available()) {
    uint8_t buf[32];
    radio.read(buf, 32);
    slots[activeSlot].hits++;
    slots[activeSlot].lastHitAt = now;
    int v = (int)slots[activeSlot].intensity + INTENSITY_BUMP;
    if (v > 255) v = 255;
    slots[activeSlot].intensity = (uint8_t)v;
  }

  // Periodic intensity decay
  if (now - lastDecayMs >= INTENSITY_DECAY_PERIOD_MS) {
    for (int s = 0; s < 3; s++) {
      if (slots[s].intensity >= INTENSITY_DECAY) slots[s].intensity -= INTENSITY_DECAY;
      else slots[s].intensity = 0;
    }
    lastDecayMs = now;
  }

  // UI refresh
  if (now - lastUiMs > 200) {
    for (int s = 0; s < 3; s++) drawSlot(s);
    lastUiMs = now;
  }

  delay(2);
}

}  // namespace NrfTriWatch
