/*
 * SD Browser — see header.
 */

#include "sd_browser.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <SD.h>
#include <SPI.h>
#include <string.h>

#define DARK_GRAY 0x4208

#define BTN_UP    6
#define BTN_DOWN  3
#define BTN_LEFT  4
#define BTN_RIGHT 5

namespace SdBrowser {

#define MAX_ENTRIES 24
#define VISIBLE_ROWS 10
#define ROW_HEIGHT 22
#define LIST_TOP 60
#define PATH_MAX_LEN 64
#define ENTRY_NAME_LEN 28

struct Entry {
  char name[ENTRY_NAME_LEN];
  uint32_t size;
  bool isDir;
};

static Entry* entries = nullptr;   // heap-alloc'd in setup so DRAM only used while feature is active
static int entryCount = 0;
static int cursor = 0;
static int viewOffset = 0;
static char curPath[PATH_MAX_LEN] = "/";
static bool sdOk = false;
static bool showingFileInfo = false;
static uint32_t fileInfoSize = 0;
static char fileInfoName[ENTRY_NAME_LEN] = {0};

static uint32_t lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 180;
static bool toggleHeldFromPrev = true;
static uint32_t lastUiMs = 0;

static void joinPath(char* out, int cap, const char* dir, const char* name) {
  if (strcmp(dir, "/") == 0) {
    snprintf(out, cap, "/%s", name);
  } else {
    snprintf(out, cap, "%s/%s", dir, name);
  }
}

static void parentPath(char* path) {
  int len = strlen(path);
  if (len <= 1) return;
  // strip trailing '/'
  if (path[len - 1] == '/') path[--len] = '\0';
  // strip trailing path component
  while (len > 0 && path[len - 1] != '/') path[--len] = '\0';
  // if we stripped everything except the leading '/', keep it
  if (len == 0) { path[0] = '/'; path[1] = '\0'; }
  else if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';
}

static void scanDir(const char* path) {
  entryCount = 0;
  if (!entries) return;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  while (entryCount < MAX_ENTRIES) {
    File f = dir.openNextFile();
    if (!f) break;
    const char* n = f.name();
    // SD.h on ESP32 returns absolute path; we want just the leaf
    const char* leaf = strrchr(n, '/');
    leaf = leaf ? leaf + 1 : n;
    strncpy(entries[entryCount].name, leaf, sizeof(entries[entryCount].name) - 1);
    entries[entryCount].name[sizeof(entries[entryCount].name) - 1] = '\0';
    entries[entryCount].size = f.size();
    entries[entryCount].isDir = f.isDirectory();
    entryCount++;
    f.close();
  }
  dir.close();
  cursor = 0;
  viewOffset = 0;
}

static void humanSize(uint32_t bytes, char* out, int cap) {
  if (bytes < 1024)            snprintf(out, cap, "%u B",   (unsigned)bytes);
  else if (bytes < 1024*1024)  snprintf(out, cap, "%.1f K", bytes / 1024.0f);
  else                         snprintf(out, cap, "%.2f M", bytes / 1048576.0f);
}

static void drawHeader() {
  tft.fillRect(0, 40, tft.width(), 18, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 45);
  tft.print("SD Browser");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(90, 45);
  tft.printf("path: %s", curPath);
  if (!sdOk) {
    tft.setTextColor(TFT_RED);
    tft.setCursor(170, 45);
    tft.print("NO SD");
  }
}

static void drawList() {
  tft.fillRect(0, LIST_TOP, tft.width(),
               tft.height() - LIST_TOP - 20, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  if (entryCount == 0) {
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(2, LIST_TOP + 4);
    tft.print(sdOk ? "(empty directory)" : "(SD card not mounted)");
    return;
  }

  int visible = entryCount - viewOffset;
  if (visible > VISIBLE_ROWS) visible = VISIBLE_ROWS;

  for (int slot = 0; slot < visible; slot++) {
    int i = viewOffset + slot;
    int y = LIST_TOP + slot * ROW_HEIGHT;
    bool sel = (i == cursor);
    if (sel) {
      tft.fillRect(0, y, tft.width(), ROW_HEIGHT, DARK_GRAY);
    }
    tft.setTextColor(sel ? TFT_YELLOW : TFT_WHITE, sel ? DARK_GRAY : TFT_BLACK);
    tft.setCursor(2, y + 6);
    tft.print(entries[i].isDir ? "[D] " : "[F] ");
    char nameShow[22];
    int nl = strlen(entries[i].name);
    if (nl > 21) nl = 21;
    memcpy(nameShow, entries[i].name, nl);
    nameShow[nl] = '\0';
    tft.print(nameShow);

    if (!entries[i].isDir) {
      char sz[16];
      humanSize(entries[i].size, sz, sizeof(sz));
      tft.setCursor(180, y + 6);
      tft.setTextColor(sel ? TFT_GREEN : TFT_DARKGREY, sel ? DARK_GRAY : TFT_BLACK);
      tft.print(sz);
    }
  }

  // Scroll indicator
  if (entryCount > VISIBLE_ROWS) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(2, LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT + 2);
    tft.printf("%d/%d", cursor + 1, entryCount);
  }
}

static void drawFooter() {
  tft.fillRect(0, tft.height() - 16, tft.width(), 16, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, tft.height() - 12);
  if (showingFileInfo) {
    tft.print("[L/R] back  [SEL] exit");
  } else {
    tft.print("[U/D] move  [R] open  [L] up  [SEL] exit");
  }
}

static void drawFileInfo() {
  tft.fillRect(0, LIST_TOP, tft.width(),
               tft.height() - LIST_TOP - 20, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, LIST_TOP + 4);
  tft.print("File info");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, LIST_TOP + 24);
  tft.printf("Name: %s", fileInfoName);

  char sz[16];
  humanSize(fileInfoSize, sz, sizeof(sz));
  tft.setCursor(2, LIST_TOP + 40);
  tft.printf("Size: %s (%u bytes)", sz, (unsigned)fileInfoSize);

  tft.setCursor(2, LIST_TOP + 56);
  tft.printf("Dir:  %s", curPath);

  // Guess type from extension and show a hint
  const char* dot = strrchr(fileInfoName, '.');
  const char* hint = nullptr;
  if (dot) {
    if      (!strcasecmp(dot, ".pcap")) hint = "tcpdump/Wireshark capture";
    else if (!strcasecmp(dot, ".csv"))  hint = "Wardrive log";
    else if (!strcasecmp(dot, ".sub"))  hint = "Flipper sub-GHz file";
    else if (!strcasecmp(dot, ".bin"))  hint = "Binary blob";
    else if (!strcasecmp(dot, ".txt"))  hint = "Plain text";
    else if (!strcasecmp(dot, ".log"))  hint = "Log file";
  }
  if (hint) {
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(2, LIST_TOP + 80);
    tft.printf("Type: %s", hint);
  }

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, LIST_TOP + 110);
  tft.print("Pull the SD to open this file");
  tft.setCursor(2, LIST_TOP + 122);
  tft.print("on a computer.");
}

// ─── Setup / Loop ───────────────────────────────────────────────────────────

void sdBrowserSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

  strcpy(curPath, "/");
  cursor = 0;
  viewOffset = 0;
  showingFileInfo = false;
  toggleHeldFromPrev = true;

  if (!entries) {
    entries = (Entry*)calloc(MAX_ENTRIES, sizeof(Entry));
  }

  // Touchscreen and SD both ride VSPI but on different pin sets
  // (touch = 25/35/32/33, SD = 18/19/23/5). setupTouchscreen() above
  // remapped VSPI to the touch pins via the GPIO matrix; we MUST
  // re-pin VSPI back for SD or SD.begin() talks to thin air.
  SPI.begin(18, 19, 23, 5);
  sdOk = SD.begin(5);
  if (!entries) {
    Serial.println("[SdBrowser] alloc failed");
    entryCount = 0;
  } else if (sdOk) {
    scanDir(curPath);
  } else {
    entryCount = 0;
    Serial.println("[SdBrowser] SD.begin() failed");
  }

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  drawHeader();
  drawList();
  drawFooter();
  lastUiMs = millis();
}

void sdBrowserLoop() {
  uint32_t now = millis();

  bool up    = !pcf.digitalRead(BTN_UP);
  bool down  = !pcf.digitalRead(BTN_DOWN);
  bool left  = !pcf.digitalRead(BTN_LEFT);
  bool right = !pcf.digitalRead(BTN_RIGHT);
  bool any = up || down || left || right;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (showingFileInfo) {
      if (left || right) {
        showingFileInfo = false;
        drawHeader();
        drawList();
        drawFooter();
      }
    } else {
      if (down && cursor < entryCount - 1) {
        cursor++;
        if (cursor - viewOffset >= VISIBLE_ROWS) viewOffset++;
        drawList();
      } else if (up && cursor > 0) {
        cursor--;
        if (cursor < viewOffset) viewOffset = cursor;
        drawList();
      } else if (right && entryCount > 0) {
        Entry& e = entries[cursor];
        if (e.isDir) {
          char next[PATH_MAX_LEN];
          joinPath(next, sizeof(next), curPath, e.name);
          strncpy(curPath, next, sizeof(curPath) - 1);
          curPath[sizeof(curPath) - 1] = '\0';
          scanDir(curPath);
          drawHeader();
          drawList();
        } else {
          strncpy(fileInfoName, e.name, sizeof(fileInfoName) - 1);
          fileInfoName[sizeof(fileInfoName) - 1] = '\0';
          fileInfoSize = e.size;
          showingFileInfo = true;
          drawHeader();
          drawFileInfo();
          drawFooter();
        }
      } else if (left) {
        if (strcmp(curPath, "/") != 0) {
          parentPath(curPath);
          scanDir(curPath);
          drawHeader();
          drawList();
        }
      }
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
  }

  if (now - lastUiMs > 1000) {
    drawHeader();
    lastUiMs = now;
  }
  delay(15);
}

}  // namespace SdBrowser
