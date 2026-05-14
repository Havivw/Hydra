/*
 * SubGHz manual code transmitter — see header.
 */

#include "sub_sendcode.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"
#include "subconfig.h"
#include "shared.h"
#include "sub_shared.h"

#include <stdio.h>
#include <string.h>

namespace SubSendCode {

// ============================================================
// Phase state
// ============================================================
enum Phase {
  PHASE_PROTO = 0,
  PHASE_FREQ,
  PHASE_CODE,
  PHASE_BITLEN,
  PHASE_READY,
  PHASE_TX
};

#define NUM_PROTOCOLS 12
static const char* PROTOCOL_LABELS[NUM_PROTOCOLS] = {
  "1: PT2262 / Princeton",
  "2: HT12 / EV1527",
  "3: 100us short",
  "4: PT2262 variant",
  "5: 500us short",
  "6: HT6 / HT8",
  "7: 23x sync",
  "8: Conrad RS-200",
  "9: 365us short",
  "10: 270us short",
  "11: 320us short",
  "12: 400us short"
};

// Typical bit length per protocol — used as the default when the user
// reaches PHASE_BITLEN so they usually don't have to touch it.
static int defaultBitlenFor(int protoIdx) {
  switch (protoIdx) {
    case 0: return 24;  // PT2262 — 24-bit is by far the most common
    case 1: return 24;  // HT12 / EV1527 — also 24
    case 5: return 24;  // HT6 / HT8
    case 7: return 32;  // Conrad — 32-bit
    default: return 24;
  }
}

static Phase    phase = PHASE_PROTO;
static int      protoIdx = 0;            // 0..11
static int      protoViewOffset = 0;
static uint16_t freqActiveIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int      freqActiveCount = 0;
static int      freqCursor = 0;
static int      freqViewOffset = 0;
static int      chosenChannelIdx = -1;
static uint32_t codeValue = 0;
static int      codeCursor = 7;          // 0..7 = hex digit, 8 = OK
static int      bitLength = 24;
static uint32_t lastBtnMs = 0;
static const uint32_t NAV_DEBOUNCE_MS = 180;
static bool     toggleHeldFromPrev = true;
static char     txStatus[80] = "";

#define LIST_VISIBLE_ROWS 11
#define LIST_ROW_HEIGHT   16
#define LIST_TOP_Y        65

// ============================================================
// Header
// ============================================================
static void drawHeader(const char *title) {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print(title);
}

static void drawHint(const char *line1, const char *line2) {
  tft.fillRect(0, tft.height() - 40, tft.width(), 30, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(2, tft.height() - 30);
  tft.print(line1);
  if (line2) {
    tft.setCursor(2, tft.height() - 15);
    tft.print(line2);
  }
}

// ============================================================
// PHASE_PROTO — scrollable protocol list
// ============================================================
static void drawProtoList() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  if (protoIdx < protoViewOffset) protoViewOffset = protoIdx;
  if (protoIdx >= protoViewOffset + LIST_VISIBLE_ROWS)
    protoViewOffset = protoIdx - LIST_VISIBLE_ROWS + 1;

  int shown = NUM_PROTOCOLS - protoViewOffset;
  if (shown > LIST_VISIBLE_ROWS) shown = LIST_VISIBLE_ROWS;

  for (int row = 0; row < shown; row++) {
    int idx = protoViewOffset + row;
    int y = LIST_TOP_Y + row * LIST_ROW_HEIGHT;
    bool selected = (idx == protoIdx);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                     selected ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf(" %s ", PROTOCOL_LABELS[idx]);
  }

  drawHint("UP/DOWN pick   RIGHT next",
           "SEL exit");
}

// ============================================================
// PHASE_FREQ — scrollable freq list (active wardrive channels)
// ============================================================
static void rebuildFreqList() {
  freqActiveCount = 0;
  for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) {
    if (WardriveConfig::channels[i].selected) {
      freqActiveIdx[freqActiveCount++] = i;
    }
  }
  if (freqActiveCount == 0) {
    for (int i = 0; i < HYDRA_WARDRIVE_CHANNEL_COUNT; i++) freqActiveIdx[i] = i;
    freqActiveCount = HYDRA_WARDRIVE_CHANNEL_COUNT;
  }
  // Default to 433.92 if present.
  freqCursor = 0;
  for (int i = 0; i < freqActiveCount; i++) {
    if (WardriveConfig::channels[freqActiveIdx[i]].hz == 433920000UL) {
      freqCursor = i;
      break;
    }
  }
  freqViewOffset = 0;
}

static void drawFreqList() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  if (freqCursor < freqViewOffset) freqViewOffset = freqCursor;
  if (freqCursor >= freqViewOffset + LIST_VISIBLE_ROWS)
    freqViewOffset = freqCursor - LIST_VISIBLE_ROWS + 1;

  int shown = freqActiveCount - freqViewOffset;
  if (shown > LIST_VISIBLE_ROWS) shown = LIST_VISIBLE_ROWS;

  for (int row = 0; row < shown; row++) {
    int idx = freqViewOffset + row;
    int chanIdx = freqActiveIdx[idx];
    uint32_t hz = WardriveConfig::channels[chanIdx].hz;
    int y = LIST_TOP_Y + row * LIST_ROW_HEIGHT;
    bool selected = (idx == freqCursor);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                     selected ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf(" %2d. %7.3f MHz ", idx + 1, hz / 1000000.0f);
  }

  drawHint("UP/DOWN pick   RIGHT next",
           "LEFT back   SEL exit");
}

// ============================================================
// PHASE_CODE — hex digit entry
// ============================================================
static void drawCodeEntry() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 75);
  tft.print("Code (hex):");

  // Render 8 hex digits left-to-right plus a virtual "[OK]" 9th slot.
  // Cursor highlights the active digit in inverse green; cursor==8 is
  // the OK slot. Each digit is shown 16 px wide so the user can read
  // the position they're editing.
  tft.setTextFont(2);
  tft.setTextSize(2);
  for (int i = 0; i < 8; i++) {
    int x = 10 + i * 18;
    bool sel = (codeCursor == i);
    int digit = (codeValue >> ((7 - i) * 4)) & 0xF;
    tft.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                     sel ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(x, 110);
    if (digit < 10) tft.printf("%d", digit);
    else            tft.printf("%c", 'A' + (digit - 10));
  }

  // OK slot
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(codeCursor == 8 ? TFT_BLACK : TFT_WHITE,
                   codeCursor == 8 ? TFT_GREEN : TFT_BLACK);
  tft.setCursor(170, 120);
  tft.print(" OK ");

  // Echo of the full value in standard hex
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 165);
  tft.printf("Value: 0x%08lX  (%lu)",
             (unsigned long)codeValue, (unsigned long)codeValue);

  drawHint("UP/DOWN digit   LEFT/RIGHT cursor",
           "Reach OK + RIGHT to confirm");
}

// ============================================================
// PHASE_BITLEN — pick bit length 4..32
// ============================================================
static void drawBitlenPick() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 80);
  tft.print("Bit length:");

  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 115);
  tft.printf("%d", bitLength);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 165);
  tft.print("Common values: 12, 24, 32");
  tft.setCursor(10, 180);
  tft.print("Most keyfobs are 24-bit.");

  drawHint("UP/DOWN change   RIGHT confirm",
           "LEFT back   SEL exit");
}

// ============================================================
// PHASE_READY — review screen
// ============================================================
static void drawReady() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, 75);
  tft.print("Ready to TX");

  uint32_t hz = WardriveConfig::channels[chosenChannelIdx].hz;
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 110);
  tft.printf("Protocol:  %s", PROTOCOL_LABELS[protoIdx]);
  tft.setCursor(10, 128);
  tft.printf("Frequency: %.3f MHz", hz / 1000000.0f);
  tft.setCursor(10, 146);
  tft.printf("Code:      0x%08lX", (unsigned long)codeValue);
  tft.setCursor(10, 164);
  tft.printf("Bit length: %d", bitLength);

  if (txStatus[0]) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 190);
    tft.print(txStatus);
  }

  drawHint("UP transmit   LEFT back",
           "SEL exit");
}

// ============================================================
// Actual TX — same pattern as SavedProfile::transmitProfile
// ============================================================
static void doTransmit() {
  uint32_t hz = WardriveConfig::channels[chosenChannelIdx].hz;
  subghzReleasePinsFromNrf();

  cc1101InitForDivV1();
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(hz / 1000000.0f);

  // RCSwitch::send drives TX_PIN directly; CC1101 in TX async serial
  // mode modulates the carrier from that line. SubReplay does the same.
  subSwitch.disableReceive();
  delay(20);
  subSwitch.enableTransmit(CC1101_GDO0_PIN);
  ELECHOUSE_cc1101.SetTx();

  // rc-switch protocols are numbered 1..12, our protoIdx is 0..11.
  subSwitch.setProtocol(protoIdx + 1);
  subSwitch.send(codeValue, (unsigned int)bitLength);

  // Re-arm RX-side state so the same instance can decode again later.
  ELECHOUSE_cc1101.setSidle();
  subSwitch.disableTransmit();

  snprintf(txStatus, sizeof(txStatus),
           "Sent 0x%lX @ %.3f MHz (P%d, %db)",
           (unsigned long)codeValue, hz / 1000000.0f,
           protoIdx + 1, bitLength);
  Serial.printf("[SendCode] %s\n", txStatus);
}

// ============================================================
// PUBLIC API
// ============================================================
void subSendCodeSetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  WardriveConfig::ensureInit();
  rebuildFreqList();

  phase = PHASE_PROTO;
  protoIdx = 0;
  protoViewOffset = 0;
  codeValue = 0;
  codeCursor = 7;
  bitLength = defaultBitlenFor(protoIdx);
  toggleHeldFromPrev = true;
  txStatus[0] = '\0';

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader("Send Code  [protocol]");
  drawProtoList();
}

void subSendCodeLoop() {
  uint32_t now = millis();
  bool upPressed    = !pcf.digitalRead(BTN_UP);
  bool downPressed  = !pcf.digitalRead(BTN_DOWN);
  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool any = upPressed || downPressed || leftPressed || rightPressed;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
    delay(15);
    return;
  }

  if (phase == PHASE_PROTO) {
    bool redraw = false;
    if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      protoIdx = (protoIdx + 1) % NUM_PROTOCOLS;
      lastBtnMs = now; redraw = true;
    } else if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      protoIdx = (protoIdx - 1 + NUM_PROTOCOLS) % NUM_PROTOCOLS;
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      bitLength = defaultBitlenFor(protoIdx);
      phase = PHASE_FREQ;
      toggleHeldFromPrev = true;
      drawHeader("Send Code  [frequency]");
      drawFreqList();
      return;
    }
    if (redraw) drawProtoList();
  }
  else if (phase == PHASE_FREQ) {
    bool redraw = false;
    if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      freqCursor = (freqCursor + 1) % freqActiveCount;
      lastBtnMs = now; redraw = true;
    } else if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      freqCursor = (freqCursor - 1 + freqActiveCount) % freqActiveCount;
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      chosenChannelIdx = freqActiveIdx[freqCursor];
      phase = PHASE_CODE;
      toggleHeldFromPrev = true;
      codeCursor = 7;  // cursor on the rightmost digit so UP/DOWN edits low nibble first
      drawHeader("Send Code  [code]");
      drawCodeEntry();
      return;
    } else if (leftPressed) {
      phase = PHASE_PROTO;
      toggleHeldFromPrev = true;
      drawHeader("Send Code  [protocol]");
      drawProtoList();
      return;
    }
    if (redraw) drawFreqList();
  }
  else if (phase == PHASE_CODE) {
    bool redraw = false;
    if (codeCursor == 8) {
      // OK slot
      if (rightPressed) {
        phase = PHASE_BITLEN;
        toggleHeldFromPrev = true;
        drawHeader("Send Code  [bit length]");
        drawBitlenPick();
        return;
      } else if (leftPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        codeCursor = 7;
        lastBtnMs = now; redraw = true;
      }
    } else {
      // 0..7 — editing a hex digit
      int shift = (7 - codeCursor) * 4;
      uint32_t mask = (uint32_t)0xF << shift;
      uint32_t digit = (codeValue >> shift) & 0xF;

      if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        digit = (digit + 1) & 0xF;
        codeValue = (codeValue & ~mask) | (digit << shift);
        lastBtnMs = now; redraw = true;
      } else if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        digit = (digit - 1) & 0xF;
        codeValue = (codeValue & ~mask) | (digit << shift);
        lastBtnMs = now; redraw = true;
      } else if (rightPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        codeCursor++;  // 7 -> 8 (OK), wraps to 0 only via LEFT
        if (codeCursor > 8) codeCursor = 0;
        lastBtnMs = now; redraw = true;
      } else if (leftPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        codeCursor--;
        if (codeCursor < 0) codeCursor = 8;
        lastBtnMs = now; redraw = true;
      }
    }
    if (redraw) drawCodeEntry();
  }
  else if (phase == PHASE_BITLEN) {
    bool redraw = false;
    if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      if (bitLength < 32) bitLength++;
      lastBtnMs = now; redraw = true;
    } else if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      if (bitLength > 4) bitLength--;
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      phase = PHASE_READY;
      toggleHeldFromPrev = true;
      txStatus[0] = '\0';
      drawHeader("Send Code  [ready]");
      drawReady();
      return;
    } else if (leftPressed) {
      phase = PHASE_CODE;
      toggleHeldFromPrev = true;
      drawHeader("Send Code  [code]");
      drawCodeEntry();
      return;
    }
    if (redraw) drawBitlenPick();
  }
  else if (phase == PHASE_READY) {
    if (upPressed) {
      // Transmit
      phase = PHASE_TX;
      toggleHeldFromPrev = true;
      strncpy(txStatus, "Transmitting...", sizeof(txStatus));
      drawHeader("Send Code  [transmitting]");
      drawReady();
      doTransmit();
      phase = PHASE_READY;
      drawHeader("Send Code  [ready]");
      drawReady();
      return;
    } else if (leftPressed) {
      phase = PHASE_BITLEN;
      toggleHeldFromPrev = true;
      drawHeader("Send Code  [bit length]");
      drawBitlenPick();
      return;
    }
  }

  delay(15);
}

}  // namespace SubSendCode
