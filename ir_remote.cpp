/*
 * IR Remote — see header.
 */

#include "ir_remote.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#define DARK_GRAY 0x4208

// Pin choice on cifertech v1 board:
//   GPIO 14 is TFT_SCLK on this PCB — using it for IR_TX kills the
//   display. Upstream shared.h's IR_TX=14 was for the v2 board.
//   GPIO 22 is free; wire an external IR LED (with ~100Ω series
//   resistor) anode-to-22 cathode-to-GND for physical TX.
//   GPIO 21 is also free; wire a TSOP1738 (or any 38 kHz IR receiver)
//   data pin to it.
#define IR_RX_PIN 21
#define IR_TX_PIN 22

#define BTN_UP    6
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 5

namespace IrRemote {

enum Mode { MODE_RECV = 0, MODE_REPLAY = 1, MODE_TVOFF = 2 };

static IRrecv  irRecv(IR_RX_PIN, 1024, 50, true);
static IRsend  irSend(IR_TX_PIN);

static Mode      mode = MODE_RECV;
static bool      attackActive = false;   // auto-replay/TV-off loop
static uint32_t  totalTx = 0;
static uint32_t  totalRx = 0;

// Last decoded capture
static decode_results lastResult;
static bool      haveCapture = false;
static decode_type_t capProto = decode_type_t::UNKNOWN;
static uint64_t  capValue = 0;
static uint16_t  capBits = 0;
static uint16_t  capRaw[100];
static uint16_t  capRawLen = 0;

static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool      toggleHeldFromPrev = true;
static uint32_t  lastUiMs = 0;
static uint32_t  lastTxMs = 0;

// ─── TV-B-Gone Lite — common power-off codes ────────────────────────────────
// Each entry: protocol, 32-bit value, bits. IRremoteESP8266 sends accordingly.
// Covers Samsung, LG, Sony, Panasonic, NEC, RCA, Sharp, Vizio, JVC, Toshiba.

struct TvCode {
  decode_type_t proto;
  uint32_t value;
  uint8_t bits;
};

static const TvCode tvCodes[] = {
  { decode_type_t::NEC,        0x20DF10EF, 32 },  // LG
  { decode_type_t::NEC,        0x10EF8877, 32 },  // RCA
  { decode_type_t::NEC,        0x20DF23DC, 32 },  // LG alt
  { decode_type_t::NEC,        0x807FE817, 32 },  // NEC generic
  { decode_type_t::SAMSUNG,    0xE0E040BF, 32 },  // Samsung TV
  { decode_type_t::SAMSUNG,    0xE0E019E6, 32 },  // Samsung alt
  { decode_type_t::SONY,       0xA90,      12 },  // Sony 12-bit TV
  { decode_type_t::SONY,       0xA8B,      15 },  // Sony 15-bit TV
  { decode_type_t::SONY,       0x290,      12 },  // Sony VCR
  { decode_type_t::PANASONIC,  0x100BCBD,  48 },  // Panasonic TV power
  { decode_type_t::JVC,        0xC5E8,     16 },  // JVC TV
  { decode_type_t::SHARP,      0x41A2,     15 },  // Sharp
  { decode_type_t::RC5,        0x100C,     13 },  // RC5 power
  { decode_type_t::RC6,        0xC,        20 },  // RC6 power
  { decode_type_t::NEC,        0x57E3E817, 32 },  // Vizio
  { decode_type_t::NEC,        0x4FB04FB,  32 },  // Toshiba
  { decode_type_t::NEC,        0x35CD7887, 32 },  // Hitachi
  { decode_type_t::NEC,        0xC1AA09F6, 32 },  // Mitsubishi
  { decode_type_t::NEC,        0xA55A38C7, 32 },  // Daewoo
  { decode_type_t::NEC,        0xB24DBF40, 32 },  // Magnavox
  { decode_type_t::NEC,        0x10AF8F70, 32 },  // Sanyo
  { decode_type_t::NEC,        0xC1AA39C6, 32 },  // Philips NEC
  { decode_type_t::NEC,        0xFD807F,   32 },  // Akai
  { decode_type_t::NEC,        0x2FD48B7,  32 },  // Insignia
  { decode_type_t::NEC,        0x4FB48B7,  32 },  // Element
  { decode_type_t::SAMSUNG,    0xE0E0F00F, 32 },  // Samsung alt2
  { decode_type_t::NEC,        0x7E817487, 32 },  // GE
  { decode_type_t::NEC,        0x1FE48B7,  32 },  // Polaroid
  { decode_type_t::NEC,        0x8166817E, 32 },  // Westinghouse
  { decode_type_t::SONY,       0xF0A50,    20 }   // Sony 20-bit
};
static const int tvCodeCount = sizeof(tvCodes) / sizeof(tvCodes[0]);
static int tvIdx = 0;

static const char* protoName(decode_type_t p) {
  switch (p) {
    case decode_type_t::NEC:       return "NEC";
    case decode_type_t::SONY:      return "SONY";
    case decode_type_t::SAMSUNG:   return "SAMSUNG";
    case decode_type_t::PANASONIC: return "PANASONIC";
    case decode_type_t::JVC:       return "JVC";
    case decode_type_t::SHARP:     return "SHARP";
    case decode_type_t::RC5:       return "RC5";
    case decode_type_t::RC6:       return "RC6";
    case decode_type_t::LG:        return "LG";
    default:                       return "UNK";
  }
}

static void sendTvCode(const TvCode& c) {
  switch (c.proto) {
    case decode_type_t::NEC:       irSend.sendNEC(c.value, c.bits); break;
    case decode_type_t::SONY:      irSend.sendSony(c.value, c.bits, 2); break;
    case decode_type_t::SAMSUNG:   irSend.sendSAMSUNG(c.value, c.bits); break;
    case decode_type_t::PANASONIC: irSend.sendPanasonic64(c.value, c.bits); break;
    case decode_type_t::JVC:       irSend.sendJVC(c.value, c.bits); break;
    case decode_type_t::SHARP:     irSend.sendSharpRaw(c.value, c.bits); break;
    case decode_type_t::RC5:       irSend.sendRC5(c.value, c.bits); break;
    case decode_type_t::RC6:       irSend.sendRC6(c.value, c.bits); break;
    case decode_type_t::LG:        irSend.sendLG(c.value, c.bits); break;
    default: break;
  }
  totalTx++;
}

static void sendCapturedRaw() {
  if (!haveCapture || capRawLen < 2) return;
  irSend.sendRaw(capRaw, capRawLen, 38);  // 38 kHz
  totalTx++;
}

// ─── UI ─────────────────────────────────────────────────────────────────────

static const char* modeName() {
  switch (mode) {
    case MODE_RECV:   return "RECEIVE";
    case MODE_REPLAY: return "REPLAY";
    case MODE_TVOFF:  return "TV-OFF";
  }
  return "?";
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 18, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.printf("IR Remote   [%s]   %s",
             modeName(),
             attackActive ? "ACTIVE" : "idle");
}

static void drawBody() {
  tft.fillRect(0, 62, tft.width(), tft.height() - 75, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 68);
  tft.printf("RX=%d  TX=%d  carrier=38kHz", IR_RX_PIN, IR_TX_PIN);

  if (mode == MODE_RECV) {
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 90);
    tft.print("Point an IR remote at the");
    tft.setCursor(2, 103);
    tft.print("RX sensor and press a key.");

    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 130);
    tft.printf("RX count: %u", (unsigned)totalRx);

    if (haveCapture) {
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(2, 155);
      tft.printf("Last: %s (%u bits)", protoName(capProto), (unsigned)capBits);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(2, 170);
      tft.printf("Value: 0x%08llX",
                 (unsigned long long)capValue);
      tft.setCursor(2, 185);
      tft.printf("Raw samples: %u", (unsigned)capRawLen);
    } else {
      tft.setTextColor(TFT_DARKGREY);
      tft.setCursor(2, 155);
      tft.print("(no capture yet)");
    }

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 230);
    tft.print("Back via IR menu to Replay it");
    tft.setCursor(2, 245);
    tft.print("[SEL]  Exit");
  } else if (mode == MODE_REPLAY) {
    if (!haveCapture) {
      tft.setTextColor(TFT_RED);
      tft.setCursor(2, 95);
      tft.print("No capture loaded.");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(2, 110);
      tft.print("Switch to RECEIVE first.");
    } else {
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(2, 95);
      tft.printf("Loaded: %s (%u bits)",
                 protoName(capProto), (unsigned)capBits);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(2, 110);
      tft.printf("Value:  0x%08llX",
                 (unsigned long long)capValue);
      tft.setTextColor(TFT_CYAN);
      tft.setCursor(2, 135);
      tft.printf("Sent:   %u", (unsigned)totalTx);
    }
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 200);
    tft.print("[UP]   Fire once / toggle auto");
    tft.setCursor(2, 215);
    tft.print("[DOWN] Stop auto-fire");
    tft.setCursor(2, 245);
    tft.print("[SEL]  Exit");
  } else {  // MODE_TVOFF
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 90);
    tft.print("TV-B-Gone Lite");
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, 105);
    tft.printf("%d power codes in rotation", tvCodeCount);

    const TvCode& cur = tvCodes[tvIdx];
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(2, 130);
    tft.printf("Now: [%d/%d] %s",
               tvIdx + 1, tvCodeCount, protoName(cur.proto));
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 145);
    tft.printf("Code 0x%08lX (%u bits)",
               (unsigned long)cur.value, (unsigned)cur.bits);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(2, 165);
    tft.printf("Sent: %u", (unsigned)totalTx);

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(2, 200);
    tft.print("[UP]   Start/stop sweep");
    tft.setCursor(2, 215);
    tft.print("[DOWN] Skip to next code");
    tft.setCursor(2, 245);
    tft.print("[SEL]  Exit");
  }
}

// ─── Setup / Loop ───────────────────────────────────────────────────────────

static void commonSetup(Mode m) {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  mode = m;
  attackActive = false;
  // Persist captures across sub-tools: don't wipe totals/tvIdx; they reset
  // only on first entry to the IR section (we keep them across mode switches).
  toggleHeldFromPrev = true;

  pinMode(IR_TX_PIN, OUTPUT);
  digitalWrite(IR_TX_PIN, LOW);
  irSend.begin();
  irRecv.enableIRIn();

  Serial.printf("[IR] mode=%d ready\n", (int)m);

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawBody();
  lastUiMs = millis();
}

void irRecvSetup()   { commonSetup(MODE_RECV); }
void irReplaySetup() { commonSetup(MODE_REPLAY); }
void irTvOffSetup()  { commonSetup(MODE_TVOFF); tvIdx = 0; }

static void commonLoop();
void irRecvLoop()   { commonLoop(); }
void irReplayLoop() { commonLoop(); }
void irTvOffLoop()  { commonLoop(); }

static void commonLoop() {
  uint32_t now = millis();

  // Drain receiver
  if (mode == MODE_RECV && irRecv.decode(&lastResult)) {
    capProto = lastResult.decode_type;
    capValue = lastResult.value;
    capBits  = lastResult.bits;
    capRawLen = lastResult.rawlen > 100 ? 100 : lastResult.rawlen;
    // resultToRawArray converts ticks to microseconds — we mimic by copying
    // raw timings (skip leading 0 marker entry)
    if (lastResult.rawlen > 1) {
      uint16_t n = capRawLen;
      for (uint16_t i = 1; i < n; i++) {
        uint32_t us = (uint32_t)lastResult.rawbuf[i] * kRawTick;
        capRaw[i - 1] = (us > 0xFFFF) ? 0xFFFF : us;
      }
      capRawLen = n - 1;
    }
    haveCapture = true;
    totalRx++;
    Serial.printf("[IR] RX %s  0x%llX  %u bits  raw=%u\n",
                  protoName(capProto), (unsigned long long)capValue,
                  (unsigned)capBits, (unsigned)capRawLen);
    irRecv.resume();
    drawBody();
  }

  // Buttons (wait-for-release pattern)
  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    // L/R no longer cycle modes — each main-menu IR entry is a locked mode.
    if (up) {
      if (mode == MODE_REPLAY) {
        if (haveCapture) {
          sendCapturedRaw();
          attackActive = !attackActive;
          Serial.printf("[IR] replay auto-fire=%d\n", attackActive);
        }
      } else if (mode == MODE_TVOFF) {
        attackActive = !attackActive;
        Serial.printf("[IR] TV-off sweep=%d\n", attackActive);
      }
      drawHeader();
      drawBody();
    } else if (down) {
      if (mode == MODE_REPLAY) {
        attackActive = false;
      } else if (mode == MODE_TVOFF) {
        tvIdx = (tvIdx + 1) % tvCodeCount;
        drawBody();
      }
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
  }

  // Auto-fire / sweep
  if (attackActive && now - lastTxMs > 250) {
    lastTxMs = now;
    if (mode == MODE_REPLAY && haveCapture) {
      sendCapturedRaw();
    } else if (mode == MODE_TVOFF) {
      sendTvCode(tvCodes[tvIdx]);
      tvIdx = (tvIdx + 1) % tvCodeCount;
    }
  }

  if (now - lastUiMs > 250) {
    drawHeader();
    if (mode == MODE_TVOFF || mode == MODE_REPLAY) drawBody();
    lastUiMs = now;
  }

  delay(2);
}

}  // namespace IrRemote
