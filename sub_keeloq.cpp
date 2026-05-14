/*
 * Manual KeeLoq transmitter — see header.
 */

#include "sub_keeloq.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"
#include "subconfig.h"
#include "shared.h"
#include "sub_shared.h"
#include "keeloq_common.h"
#include "keeloq_decode.h"
#include "keeloq_keys.h"
#include "keeloq_pwm.h"

#include <stdio.h>
#include <string.h>

namespace SubKeeloq {

// ============================================================
// Phase state
// ============================================================
enum Phase {
  PHASE_NO_KEYS = 0,   // keystore empty — show banner + exit hint
  PHASE_MFR,           // pick manufacturer
  PHASE_FREQ,          // pick frequency
  PHASE_SERIAL,        // enter 28-bit serial as 7 hex digits
  PHASE_BUTTON,        // pick 4-bit button code
  PHASE_COUNTER,       // pick 16-bit counter
  PHASE_READY,         // review + TX
  PHASE_TX             // brief — replaced by READY when done
};

static Phase    phase                = PHASE_MFR;
static int      mfrCursor            = 0;
static int      mfrViewOffset        = 0;
static uint16_t freqActiveIdx[HYDRA_WARDRIVE_CHANNEL_COUNT];
static int      freqActiveCount      = 0;
static int      freqCursor           = 0;
static int      freqViewOffset       = 0;
static int      chosenChannelIdx     = -1;
static uint32_t serialValue          = 0;
static int      serialCursor         = 6;   // 0..6 = hex digit, 7 = OK
static int      buttonValue          = 0;   // 0..15
static uint16_t counterValue         = 1;
static uint32_t lastBtnMs            = 0;
static const uint32_t NAV_DEBOUNCE_MS = 180;
static bool     toggleHeldFromPrev   = true;
static char     txStatus[80]         = "";

#define LIST_VISIBLE_ROWS 11
#define LIST_ROW_HEIGHT   16
#define LIST_TOP_Y        65

// ============================================================
// Active frequency list — selected wardrive channels, default-biased
// to 433.92 MHz like SubRecord and SubSendCode.
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
  freqCursor = 0;
  for (int i = 0; i < freqActiveCount; i++) {
    if (WardriveConfig::channels[freqActiveIdx[i]].hz == 433920000UL) {
      freqCursor = i;
      break;
    }
  }
  freqViewOffset = 0;
}

// ============================================================
// Header + hint helpers
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
// PHASE_NO_KEYS — banner when keystore is empty
// ============================================================
static void drawNoKeys() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED);
  tft.setCursor(10, 80);
  tft.print("No KeeLoq keys loaded");

  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 120);
  tft.print("Drop a file at:");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 138);
  tft.print("  /hydra/keeloq_keys.txt");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 165);
  tft.print("Format (one per line):");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 183);
  tft.print("  <16-hex-key>:<type>:<name>");

  drawHint("SEL: exit", nullptr);
}

// ============================================================
// PHASE_MFR — scrollable mfr list from keystore
// ============================================================
static void drawMfrList() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  int n = KeeloqKeys::count();
  if (mfrCursor < mfrViewOffset) mfrViewOffset = mfrCursor;
  if (mfrCursor >= mfrViewOffset + LIST_VISIBLE_ROWS)
    mfrViewOffset = mfrCursor - LIST_VISIBLE_ROWS + 1;

  int shown = n - mfrViewOffset;
  if (shown > LIST_VISIBLE_ROWS) shown = LIST_VISIBLE_ROWS;

  for (int row = 0; row < shown; row++) {
    int idx = mfrViewOffset + row;
    const KeeloqKeys::Entry& e = KeeloqKeys::at(idx);
    int y = LIST_TOP_Y + row * LIST_ROW_HEIGHT;
    bool selected = (idx == mfrCursor);
    tft.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                     selected ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(2, y);
    tft.printf(" %3d. %s (t%u) ", idx + 1, e.name, (unsigned)e.learningType);
  }

  drawHint("UP/DOWN pick   RIGHT next", "SEL exit");
}

// ============================================================
// PHASE_FREQ — same shape as SubRecord/SubSendCode
// ============================================================
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

  drawHint("UP/DOWN pick   RIGHT next", "LEFT back   SEL exit");
}

// ============================================================
// PHASE_SERIAL — 7-digit hex entry for 28-bit serial
// ============================================================
static void drawSerialEntry() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 75);
  tft.print("Serial (28-bit):");

  tft.setTextFont(2);
  tft.setTextSize(2);
  for (int i = 0; i < 7; i++) {
    int x = 10 + i * 20;
    bool sel = (serialCursor == i);
    int digit = (serialValue >> ((6 - i) * 4)) & 0xF;
    tft.setTextColor(sel ? TFT_BLACK : TFT_WHITE,
                     sel ? TFT_GREEN : TFT_BLACK);
    tft.setCursor(x, 110);
    if (digit < 10) tft.printf("%d", digit);
    else            tft.printf("%c", 'A' + (digit - 10));
  }

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(serialCursor == 7 ? TFT_BLACK : TFT_WHITE,
                   serialCursor == 7 ? TFT_GREEN : TFT_BLACK);
  tft.setCursor(170, 120);
  tft.print(" OK ");

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 165);
  tft.printf("Value: 0x%07lX",
             (unsigned long)(serialValue & 0x0FFFFFFFu));

  drawHint("UP/DOWN digit   LEFT/RIGHT cursor",
           "Reach OK + RIGHT to confirm");
}

// ============================================================
// PHASE_BUTTON — scroll 0..15
// ============================================================
static void drawButtonPick() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 80);
  tft.print("Button code (0-F):");

  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 115);
  tft.printf("0x%X", buttonValue);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 165);
  tft.print("Many KeeLoq remotes use 1, 2, 4, 8");
  tft.setCursor(10, 180);
  tft.print("(one bit per physical button)");

  drawHint("UP/DOWN change   RIGHT next",
           "LEFT back   SEL exit");
}

// ============================================================
// PHASE_COUNTER — scroll 16-bit counter
// ============================================================
static void drawCounterPick() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 80);
  tft.print("Counter (16-bit):");

  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 115);
  tft.printf("%u", (unsigned)counterValue);

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(10, 165);
  tft.print("UP/DOWN: +/- 1   LEFT: +/- 16");
  tft.setCursor(10, 180);
  tft.print("Real receivers accept a small window");
  tft.setCursor(10, 195);
  tft.print("around their last-seen counter.");

  drawHint("UP/DOWN/LEFT change   RIGHT confirm",
           "SEL exit");
}

// ============================================================
// PHASE_READY — review + transmit
// ============================================================
static void drawReady() {
  tft.fillRect(0, 60, tft.width(), tft.height() - 100, TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(10, 75);
  tft.print("Ready to TX");

  const KeeloqKeys::Entry& mfr = KeeloqKeys::at(mfrCursor);
  uint32_t hz = WardriveConfig::channels[chosenChannelIdx].hz;

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 108);
  tft.printf("Mfr:    %s (t%u)", mfr.name, (unsigned)mfr.learningType);
  tft.setCursor(10, 124);
  tft.printf("Freq:   %.3f MHz", hz / 1000000.0f);
  tft.setCursor(10, 140);
  tft.printf("Serial: 0x%07lX",
             (unsigned long)(serialValue & 0x0FFFFFFFu));
  tft.setCursor(10, 156);
  tft.printf("Button: 0x%X", buttonValue);
  tft.setCursor(10, 172);
  tft.printf("Counter: %u", (unsigned)counterValue);

  if (txStatus[0]) {
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 195);
    tft.print(txStatus);
  }

  drawHint("UP transmit (advances counter)",
           "LEFT back   SEL exit");
}

// ============================================================
// Actual TX — derive per-device key, encrypt hop, synthesise PWM
// ============================================================
static void doTransmit() {
  const KeeloqKeys::Entry& mfr = KeeloqKeys::at(mfrCursor);
  uint32_t hz = WardriveConfig::channels[chosenChannelIdx].hz;

  // Derive per-device key using the mfr's learning type. We pass
  // seed=0 for the Secure / FAAC variants — manual entry can't
  // recover the original seed, so receivers that depend on it will
  // ignore this transmission. (Capture-derived replay via Record
  // doesn't have this problem because the device key is already
  // baked into the .sub file.)
  uint64_t devKey;
  switch (mfr.learningType) {
    case Keeloq::LearningSimple:
    case Keeloq::LearningSimpleKingGates:
      devKey = mfr.mfrKey; break;
    case Keeloq::LearningNormal:
    case Keeloq::LearningNormalJarolift:
      devKey = Keeloq::normalLearning(serialValue & 0x0FFFFFFFu, mfr.mfrKey); break;
    case Keeloq::LearningSecure:
      devKey = Keeloq::secureLearning(serialValue & 0x0FFFFFFFu, 0, mfr.mfrKey); break;
    case Keeloq::LearningMagicXorType1:
      devKey = Keeloq::magicXorType1Learning(serialValue & 0x0FFFFFFFu, mfr.mfrKey); break;
    case Keeloq::LearningFaac:
      devKey = Keeloq::faacLearning(0, mfr.mfrKey); break;
    case Keeloq::LearningMagicSerialType1:
      devKey = Keeloq::magicSerialType1Learning(serialValue & 0x0FFFFFFFu, mfr.mfrKey); break;
    case Keeloq::LearningMagicSerialType2:
      devKey = Keeloq::magicSerialType2Learning(serialValue & 0x0FFFFFFFu, mfr.mfrKey); break;
    case Keeloq::LearningMagicSerialType3:
      devKey = Keeloq::magicSerialType3Learning(serialValue & 0x0FFFFFFFu, mfr.mfrKey); break;
    default:
      devKey = mfr.mfrKey; break;
  }

  // Build cleartext hop (HCS301 layout):
  //   bits 31..28: button (4)
  //   bits 27..26: status (2) — VLOW/RPT, both 0 for new transmissions
  //   bits 25..16: serial[0..9] (10) — discrimination bits
  //   bits 15..0 : counter
  uint32_t cleartextHop =
      ((uint32_t)(buttonValue & 0xFu) << 28) |
      ((uint32_t)(serialValue & 0x3FFu) << 16) |
      (uint32_t)counterValue;
  uint32_t encryptedHop = Keeloq::encrypt(cleartextHop, devKey);

  KeeloqDecode::Frame fr;
  fr.encryptedHop = encryptedHop;
  fr.serial       = serialValue & 0x0FFFFFFFu;
  fr.button       = (uint8_t)(buttonValue & 0xF);
  fr.status       = 0;
  int n = KeeloqPwm::buildFrame(fr, /*teShortUs=*/400,
                                subSampleBuf, SUB_SAMPLE_BUF_MAX);
  if (n <= 0) {
    strncpy(txStatus, "PWM synth failed", sizeof(txStatus));
    return;
  }

  // CC1101 OOK 650 async TX — same setup SubReplay::cc1101TxPrep uses.
  subghzReleasePinsFromNrf();
  cc1101InitForDivV1();
  ELECHOUSE_cc1101.setMHZ(hz / 1000000.0f);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setDRate(650);
  ELECHOUSE_cc1101.setRxBW(650);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();
  pinMode(CC1101_GDO0_PIN, OUTPUT);

  // Bit-bang the synthesised samples — identical pattern to
  // SubReplay::bitbangAllSamples but inlined here so we don't have
  // to expose that helper across translation units.
  for (int i = 0; i < n; i++) {
    int s = subSampleBuf[i];
    int level = (s >= 0) ? HIGH : LOW;
    int dur   = (s >= 0) ? s : -s;
    digitalWrite(CC1101_GDO0_PIN, level);
    if (dur > 0) delayMicroseconds(dur);
  }
  digitalWrite(CC1101_GDO0_PIN, LOW);
  ELECHOUSE_cc1101.setSidle();

  snprintf(txStatus, sizeof(txStatus),
           "Sent 0x%lX cnt %u @ %.3f MHz",
           (unsigned long)encryptedHop,
           (unsigned)counterValue,
           hz / 1000000.0f);
  Serial.printf("[SendKeeLoq] %s\n", txStatus);

  // Auto-advance counter so the next UP press lands further along
  // the receiver's resync window.
  counterValue = (uint16_t)(counterValue + 1);
}

// ============================================================
// PUBLIC API
// ============================================================
void subKeeloqSetup() {
  subghzReleasePinsFromNrf();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  WardriveConfig::ensureInit();
  rebuildFreqList();

  // Lazy-load keystore the first time the feature opens.
  if (!KeeloqKeys::isLoaded()) {
    KeeloqKeys::loadFromSd();
  }

  mfrCursor = 0;
  mfrViewOffset = 0;
  serialValue = 0;
  serialCursor = 6;
  buttonValue = 0;
  counterValue = 1;
  chosenChannelIdx = -1;
  toggleHeldFromPrev = true;
  txStatus[0] = '\0';

  float v = readBatteryVoltage();
  drawStatusBar(v, false);

  if (KeeloqKeys::count() <= 0) {
    phase = PHASE_NO_KEYS;
    drawHeader("Send KeeLoq  [no keys]");
    drawNoKeys();
  } else {
    phase = PHASE_MFR;
    drawHeader("Send KeeLoq  [mfr]");
    drawMfrList();
  }
}

void subKeeloqLoop() {
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

  if (phase == PHASE_NO_KEYS) {
    // Any button just sits here; SELECT (handled in Hydra.ino) exits.
    delay(50);
    return;
  }

  if (phase == PHASE_MFR) {
    bool redraw = false;
    int n = KeeloqKeys::count();
    if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      mfrCursor = (mfrCursor + 1) % n;
      lastBtnMs = now; redraw = true;
    } else if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      mfrCursor = (mfrCursor - 1 + n) % n;
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      phase = PHASE_FREQ;
      toggleHeldFromPrev = true;
      drawHeader("Send KeeLoq  [frequency]");
      drawFreqList();
      return;
    }
    if (redraw) drawMfrList();
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
      phase = PHASE_SERIAL;
      toggleHeldFromPrev = true;
      serialCursor = 6;
      drawHeader("Send KeeLoq  [serial]");
      drawSerialEntry();
      return;
    } else if (leftPressed) {
      phase = PHASE_MFR;
      toggleHeldFromPrev = true;
      drawHeader("Send KeeLoq  [mfr]");
      drawMfrList();
      return;
    }
    if (redraw) drawFreqList();
  }
  else if (phase == PHASE_SERIAL) {
    bool redraw = false;
    if (serialCursor == 7) {
      if (rightPressed) {
        phase = PHASE_BUTTON;
        toggleHeldFromPrev = true;
        drawHeader("Send KeeLoq  [button]");
        drawButtonPick();
        return;
      } else if (leftPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        serialCursor = 6;
        lastBtnMs = now; redraw = true;
      }
    } else {
      int shift = (6 - serialCursor) * 4;
      uint32_t mask = (uint32_t)0xF << shift;
      uint32_t digit = (serialValue >> shift) & 0xF;
      if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        digit = (digit + 1) & 0xF;
        serialValue = (serialValue & ~mask) | (digit << shift);
        lastBtnMs = now; redraw = true;
      } else if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        digit = (digit - 1) & 0xF;
        serialValue = (serialValue & ~mask) | (digit << shift);
        lastBtnMs = now; redraw = true;
      } else if (rightPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        serialCursor++;
        if (serialCursor > 7) serialCursor = 0;
        lastBtnMs = now; redraw = true;
      } else if (leftPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
        serialCursor--;
        if (serialCursor < 0) serialCursor = 7;
        lastBtnMs = now; redraw = true;
      }
    }
    if (redraw) drawSerialEntry();
  }
  else if (phase == PHASE_BUTTON) {
    bool redraw = false;
    if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      buttonValue = (buttonValue + 1) & 0xF;
      lastBtnMs = now; redraw = true;
    } else if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      buttonValue = (buttonValue - 1) & 0xF;
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      phase = PHASE_COUNTER;
      toggleHeldFromPrev = true;
      drawHeader("Send KeeLoq  [counter]");
      drawCounterPick();
      return;
    } else if (leftPressed) {
      phase = PHASE_SERIAL;
      toggleHeldFromPrev = true;
      drawHeader("Send KeeLoq  [serial]");
      drawSerialEntry();
      return;
    }
    if (redraw) drawButtonPick();
  }
  else if (phase == PHASE_COUNTER) {
    bool redraw = false;
    if (upPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      counterValue = (uint16_t)(counterValue + 1);
      lastBtnMs = now; redraw = true;
    } else if (downPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      counterValue = (uint16_t)(counterValue - 1);
      lastBtnMs = now; redraw = true;
    } else if (leftPressed && now - lastBtnMs > NAV_DEBOUNCE_MS) {
      counterValue = (uint16_t)(counterValue + 16);
      lastBtnMs = now; redraw = true;
    } else if (rightPressed) {
      phase = PHASE_READY;
      toggleHeldFromPrev = true;
      txStatus[0] = '\0';
      drawHeader("Send KeeLoq  [ready]");
      drawReady();
      return;
    }
    if (redraw) drawCounterPick();
  }
  else if (phase == PHASE_READY) {
    if (upPressed) {
      phase = PHASE_TX;
      toggleHeldFromPrev = true;
      strncpy(txStatus, "Transmitting...", sizeof(txStatus));
      drawHeader("Send KeeLoq  [transmitting]");
      drawReady();
      doTransmit();
      phase = PHASE_READY;
      drawHeader("Send KeeLoq  [ready]");
      drawReady();
      return;
    } else if (leftPressed) {
      phase = PHASE_COUNTER;
      toggleHeldFromPrev = true;
      drawHeader("Send KeeLoq  [counter]");
      drawCounterPick();
      return;
    }
  }

  delay(15);
}

}  // namespace SubKeeloq
