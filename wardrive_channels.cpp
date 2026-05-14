/*
 * Wardrive Channels — see header.
 */

#include "wardrive_channels.h"
#include "wardrive_config.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#define DARK_GRAY 0x4208

#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_RIGHT  5

namespace WardriveChannels {

// Virtual rows: 3 preset shortcuts at the top, then the 30 channels.
//   0: "Select ALL"
//   1: "Select NONE"
//   2: "Restore defaults"
//   3..N: actual channel rows
#define PRESET_ROW_COUNT 3
#define TOTAL_ROWS (PRESET_ROW_COUNT + HYDRA_WARDRIVE_CHANNEL_COUNT)

#define ROW_HEIGHT 18
#define ROW_TOP_Y 60
#define VISIBLE_ROWS 12

static int cursor = 0;
static int viewOffset = 0;
static uint32_t lastNavMs = 0;
static const uint32_t NAV_DEBOUNCE_MS = 180;

// Wait-for-release state for the toggle buttons (LEFT/RIGHT). Without this
// a single finger-press lasts long enough to fire the toggle multiple
// times during one press, making selections flicker. We require the user
// to physically release the button before another toggle is accepted.
static bool toggleHeldFromPrev = true;  // assume held at entry; first poll clears it

static const char* presetLabel(int rowIdx) {
  switch (rowIdx) {
    case 0: return "Select ALL";
    case 1: return "Select NONE";
    case 2: return "Restore defaults";
    default: return "";
  }
}

static void drawRow(int slot, int rowIdx) {
  int y = ROW_TOP_Y + slot * ROW_HEIGHT;
  tft.fillRect(0, y, tft.width(), ROW_HEIGHT, TFT_BLACK);

  bool highlighted = (rowIdx == cursor);
  uint16_t fg = highlighted ? TFT_ORANGE : TFT_WHITE;
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(fg, TFT_BLACK);
  tft.setCursor(2, y + 4);

  if (rowIdx < PRESET_ROW_COUNT) {
    // Preset action row
    tft.setTextColor(highlighted ? TFT_ORANGE : TFT_CYAN, TFT_BLACK);
    tft.printf("--> %s", presetLabel(rowIdx));
    return;
  }

  int chanIdx = rowIdx - PRESET_ROW_COUNT;
  const auto& c = WardriveConfig::channels[chanIdx];
  tft.print(c.selected ? "[X] " : "[ ] ");
  tft.printf("%7.3f MHz", c.hz / 1000000.0f);
}

static void drawList() {
  tft.fillRect(0, ROW_TOP_Y, tft.width(), VISIBLE_ROWS * ROW_HEIGHT, TFT_BLACK);
  int n = TOTAL_ROWS - viewOffset;
  if (n > VISIBLE_ROWS) n = VISIBLE_ROWS;
  for (int s = 0; s < n; s++) drawRow(s, viewOffset + s);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (viewOffset > 0) {
    tft.setCursor(228, ROW_TOP_Y);
    tft.print("^");
  }
  if (viewOffset + n < TOTAL_ROWS) {
    tft.setCursor(228, ROW_TOP_Y + (n - 1) * ROW_HEIGHT);
    tft.print("v");
  }
}

static void ensureCursorVisible() {
  if (cursor < viewOffset) viewOffset = cursor;
  else if (cursor >= viewOffset + VISIBLE_ROWS) viewOffset = cursor - VISIBLE_ROWS + 1;
  int maxOff = TOTAL_ROWS - VISIBLE_ROWS;
  if (maxOff < 0) maxOff = 0;
  if (viewOffset > maxOff) viewOffset = maxOff;
  if (viewOffset < 0) viewOffset = 0;
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 20, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, 45);
  tft.printf("Wardrive Channels  %d/%d sel",
             WardriveConfig::selectedCount(),
             HYDRA_WARDRIVE_CHANNEL_COUNT);
}

void wardriveChannelsSetup() {
  WardriveConfig::ensureInit();

  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();

  cursor = PRESET_ROW_COUNT;   // start at first channel row, not on a preset
  viewOffset = 0;
  toggleHeldFromPrev = true;   // ignore the BTN_SELECT-into-this-screen carry-over

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawList();
}

void wardriveChannelsLoop() {
  uint32_t now = millis();
  bool needRedraw = false;

  bool leftPressed  = !pcf.digitalRead(BTN_LEFT);
  bool rightPressed = !pcf.digitalRead(BTN_RIGHT);
  bool togglePressed = leftPressed || rightPressed;

  // Wait-for-release on the toggle. As long as a toggle button is held
  // we ignore further presses until it's physically released.
  if (toggleHeldFromPrev) {
    if (!togglePressed) toggleHeldFromPrev = false;
  } else if (togglePressed && (now - lastNavMs > NAV_DEBOUNCE_MS)) {
    if (cursor < PRESET_ROW_COUNT) {
      switch (cursor) {
        case 0: WardriveConfig::selectAll(); break;
        case 1: WardriveConfig::selectNone(); break;
        case 2: WardriveConfig::selectDefaults(); break;
      }
    } else {
      WardriveConfig::toggleChannel(cursor - PRESET_ROW_COUNT);
    }
    lastNavMs = now;
    toggleHeldFromPrev = true;
    needRedraw = true;
  }
  // UP/DOWN keep the rolling-debounce behaviour (held = continuous scroll)
  else if (now - lastNavMs > NAV_DEBOUNCE_MS) {
    if (!pcf.digitalRead(BTN_DOWN)) {
      cursor = (cursor + 1) % TOTAL_ROWS;
      lastNavMs = now;
      needRedraw = true;
    } else if (!pcf.digitalRead(BTN_UP)) {
      cursor--;
      if (cursor < 0) cursor = TOTAL_ROWS - 1;
      lastNavMs = now;
      needRedraw = true;
    }
  }

  if (needRedraw) {
    ensureCursorVisible();
    drawHeader();
    drawList();
  }

  delay(15);
}

}  // namespace WardriveChannels
