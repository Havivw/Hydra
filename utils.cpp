#include "utils.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include "target_list.h"
#include "hydra_logo.h"
#include "version.h"


/*
 * 
 * Notification
 * 
 */


/*
    showNotification("New Message!", "Task Failed Successfully.");
    
    if (notificationVisible && ts.touched()) {
      int x, y, z;
        TS_Point p = ts.getPoint();
        x = ::map(p.x, 300, 3800, 0, 239);
        y = ::map(p.y, 3800, 300, 0, 319);
        
    if (x >= closeButtonX && x <= (closeButtonX + closeButtonSize) &&
        y >= closeButtonY && y <= (closeButtonY + closeButtonSize)) {
        hideNotification();
    }
    
    if (x >= okButtonX && x <= (okButtonX + okButtonWidth) &&
        y >= okButtonY && y <= (okButtonY + okButtonHeight)) {
        hideNotification();
    }
     delay(100);
  }
  
*/

bool notificationVisible = true;
int notifX, notifY, notifWidth, notifHeight;
int closeButtonX, closeButtonY, closeButtonSize = 15;
int okButtonX, okButtonY, okButtonWidth = 60, okButtonHeight = 20;

extern bool notificationVisible;
extern int notifX, notifY, notifWidth, notifHeight;
extern int closeButtonX, closeButtonY, closeButtonSize;
extern int okButtonX, okButtonY, okButtonWidth, okButtonHeight;


void showNotification(const char* title, const char* message) {
    notifWidth = 200;
    notifHeight = 80;
    notifX = (240 - notifWidth) / 2;
    notifY = (320 - notifHeight) / 2;

    tft.fillRect(notifX, notifY, notifWidth, notifHeight, LIGHT_GRAY);
    tft.fillRect(notifX, notifY, notifWidth, 20, BLUE);
    
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.setCursor(notifX + 5, notifY + 5);
    tft.print(title);

    closeButtonX = notifX + notifWidth - closeButtonSize - 5;
    closeButtonY = notifY + 2;
    tft.fillRect(closeButtonX, closeButtonY, closeButtonSize, closeButtonSize, RED);
    tft.setTextColor(WHITE);
    tft.setCursor(closeButtonX + 5, closeButtonY + 4);
    tft.print("X");

    int messageBoxX = notifX + 5;
    int messageBoxY = notifY + 25;
    int messageBoxWidth = notifWidth - 10;
    int messageBoxHeight = notifHeight - 45;

    tft.fillRect(messageBoxX, messageBoxY, messageBoxWidth, messageBoxHeight, WHITE);
    tft.setTextColor(BLACK);
    printWrappedText(messageBoxX + 2, messageBoxY + 5, messageBoxWidth + 2, message);

    okButtonX = notifX + (notifWidth - okButtonWidth) / 2;
    okButtonY = notifY + notifHeight - 25;

    tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
    tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
    tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
    tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);

    tft.setTextColor(BLACK);
    tft.setCursor(okButtonX + 20, okButtonY + 5);
    tft.print("OK");

    notificationVisible = true;
}

void hideNotification() {
    tft.fillRect(notifX, notifY, notifWidth, notifHeight, BLACK);
    notificationVisible = false;
}

void printWrappedText(int x, int y, int maxWidth, const char* text) {
    String message = text;  
    int cursorX = x, cursorY = y;
    
    while (message.length() > 0) {
        int lineEnd = message.length();
        
        while (tft.textWidth(message.substring(0, lineEnd)) > maxWidth) {
            lineEnd--;
        }

        if (lineEnd < message.length()) {
            int lastSpace = message.substring(0, lineEnd).lastIndexOf(' ');
            if (lastSpace > 0) lineEnd = lastSpace;
        }

        tft.setCursor(cursorX, cursorY);
        tft.print(message.substring(0, lineEnd));

        message = message.substring(lineEnd);
        message.trim();

        cursorY += 15;
    }
}


/*
 * 
 * StatusBar
 * 
 */

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
uint8_t temprature_sens_read();

unsigned long lastStatusBarUpdate = 0;
const int STATUS_BAR_UPDATE_INTERVAL = 1000; 
float lastBatteryVoltage = 0.0;
bool sdAvailable = false;

float readBatteryVoltage() {
  uint8_t temprature_sens_read();
  const int sampleCount = 10;  
  long sum = 0;
  
  for (int i = 0; i < sampleCount; i++) {
    sum += analogRead(36);  
    delay(5);  
  }
  
  float averageADC = sum / sampleCount;
  float voltage = (averageADC / 4095.0) * 3.3 * 2;  
  return voltage;
}

float readInternalTemperature() {
  float temperature = ((temprature_sens_read() - 32) / 1.8); 
  return temperature;
}

// Check if SD card is available — one-shot probe at boot, cached forever.
//
// We learned the hard way that probing every few seconds (SD.end + SD.begin)
// fights the CC1101 for the VSPI bus and produces a flickering red/green
// indicator. Worse: occasional SPI collisions can corrupt CC1101 packets
// mid-transmission. DIV v1's SD socket doesn't expose a card-detect pin,
// so there's no clean hardware way to detect hot-swap.
//
// Trade: indicator reflects the SD state at boot. To re-probe after
// inserting a card, reboot (or open Format SD / GPS Wardrive which do
// their own SD.begin and will pick up the new card).
bool isSDCardAvailable() {
  static bool sdReady    = false;
  static bool sdProbed   = false;
  if (!sdProbed) {
    sdReady  = SD.begin(5);
    sdProbed = true;
  }
  return sdReady;
}

// Returns true if the ADC reading looks like a real Li-Ion battery
// (between 2.5V and 4.5V). Outside that range the v1 board's battery pin
// is floating and the reading is just ADC noise — show "?" instead of a
// random percentage.
static bool batteryReadingPlausible(float v) {
  return v >= 2.5f && v <= 4.5f;
}

void drawStatusBar(float batteryVoltage, bool forceUpdate) {
  static int lastBatteryPercentage = -1;
  static int lastWiFiStrength = -1;
  static String lastDisplayedTime = "";
  static int lastSdState = -1;  // -1 = uninit, 0 = absent, 1 = present

  bool batteryValid = batteryReadingPlausible(batteryVoltage);
  int batteryPercentage = batteryValid
    ? constrain(map(batteryVoltage * 100, 300, 420, 0, 100), 0, 100)
    : -1;  // sentinel for "no valid reading"

  // WiFi bars driven by the strongest AP RSSI from the last AP scan.
  // 0 bars when no scan has been done (TargetList empty) — honest indicator.
  // (Original code used random() which made the bars flicker meaninglessly.)
  int wifiStrength = 0;  // 0..100
  if (TargetList::targetCount > 0) {
    int8_t bestRssi = -128;
    for (int i = 0; i < TargetList::targetCount; i++) {
      if (TargetList::targets[i].rssi > bestRssi) bestRssi = TargetList::targets[i].rssi;
    }
    // Map -90..-30 dBm to 0..100. Anything weaker than -90 stays 0; stronger
    // than -30 clamps at 100.
    if (bestRssi <= -90)      wifiStrength = 0;
    else if (bestRssi >= -30) wifiStrength = 100;
    else                      wifiStrength = (int)((bestRssi + 90) * 100 / 60);
  }
  wifiStrength = constrain(wifiStrength, 0, 100);

  float internalTemp = readInternalTemperature();
  // Match v1: do NOT probe SD in the status bar. SD.begin() internally calls
  // SPI.begin() with default VSPI pins (18/19/23/5), which re-routes VSPI
  // MISO from GPIO 35 (touch) to GPIO 19. That permanently breaks
  // touchscreen reads for the rest of the session. V1's original code had
  // `bool sdAvailable = false;` with the probe commented out for exactly
  // this reason. Individual SD-using features can still re-probe — they
  // accept losing touch in exchange for SD I/O.
  bool sdAvailable = false;
  int sdState = sdAvailable ? 1 : 0;

  if (batteryPercentage != lastBatteryPercentage ||
      wifiStrength != lastWiFiStrength ||
      sdState != lastSdState ||
      forceUpdate) {
    lastSdState = sdState;
    int barHeight = 20;  // Status bar height
    int x = 7;           // Padding for battery icon
    int y = 4;           // Vertical offset

    // **Dark Background with Neon Green Edge**
    tft.fillRect(0, 0, tft.width(), barHeight, DARK_GRAY);
    //tft.fillRect(0, barHeight - 2, tft.width(), 3, ORANGE); 

    // **Draw Battery Icon (Hacker/Techy Look)**
    tft.drawRoundRect(x, y, 22, 10, 2, TFT_WHITE);        // Battery border
    tft.fillRect(x + 22, y + 3, 2, 4, TFT_WHITE);         // Battery terminal
    
    if (batteryValid) {
      int batteryLevelWidth = map(batteryPercentage, 0, 100, 0, 20);
      uint16_t batteryColor = (batteryPercentage > 20) ? TFT_GREEN : TFT_RED;
      tft.fillRoundRect(x + 2, y + 2, batteryLevelWidth, 6, 1, batteryColor);
      tft.setCursor(x + 30, y + 2);
      tft.setTextColor(TFT_GREEN, DARK_GRAY);
      tft.setTextFont(1);
      tft.setTextSize(1);
      tft.print(String(batteryPercentage) + "%");
    } else {
      // No battery divider wired on this pin — show "?" instead of a
      // random percentage that confuses users.
      tft.setCursor(x + 30, y + 2);
      tft.setTextColor(TFT_DARKGREY, DARK_GRAY);
      tft.setTextFont(1);
      tft.setTextSize(1);
      tft.print("--");
    }

    // **Draw Wi-Fi Signal Bars (Neon Green)**
    int wifiX = 180;
    int wifiY = y + 11;
    for (int i = 0; i < 4; i++) {
      int barHeight = (i + 1) * 3;
      int barWidth = 4;
      int barX = wifiX + i * 6;
      if (wifiStrength > i * 25) {
        tft.fillRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_GREEN);  
      } else {
        tft.drawRoundRect(barX, wifiY - barHeight, barWidth, barHeight, 1, TFT_WHITE);  
      }
    }

    // ESP32 internal die sensor is uncalibrated (±10°C variance) AND only
    // measures the chip itself, not the board. Idle die runs ~40-50°C;
    // under WiFi+BT load it climbs to 60-75°C. The board frame can feel
    // hot from the regulator/charging IC long before the die does, but
    // the indicator can only see the die. Thresholds lowered so a warm
    // board doesn't stay falsely green.
    if (internalTemp >= 60.0f) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_RED);
    } else if (internalTemp >= 45.0f) {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_YELLOW);
    } else {
      tft.drawBitmap(203, y - 3, bitmap_icon_temp, 16, 16, TFT_GREEN);
    }

    // **Display SD Card Icon (If Available)**
    if (sdAvailable) {
      tft.drawBitmap(220, y - 3, bitmap_icon_sdcard, 16, 16, TFT_GREEN);
    } else {
      tft.drawBitmap(220, y - 3, bitmap_icon_nullsdcard, 16, 16, TFT_RED);
    }

    // **Bottom Line for Aesthetic (Neon Green)**
    //tft.drawLine(0, barHeight - 1, tft.width(), barHeight - 1, ORANGE);  

    // **Update Last Values**
    lastBatteryPercentage = batteryPercentage;
    lastWiFiStrength = wifiStrength;
  }
}

void updateStatusBar() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastStatusBarUpdate > STATUS_BAR_UPDATE_INTERVAL) {
    float batteryVoltage = readBatteryVoltage();

    if (abs(batteryVoltage - lastBatteryVoltage) > 0.05 || lastBatteryVoltage == 0) {
      drawStatusBar(batteryVoltage);
      lastBatteryVoltage = batteryVoltage;
    }

    lastStatusBarUpdate = currentMillis;
  }
}


/*
 * Loading — STUBBED
 *
 * The original 10-frame skull-loading animation has been gutted. The 10
 * bitmap frames in icon.h were unreferenced from here, so the linker
 * with --gc-sections drops them; the body of this function is now a tiny
 * delay so legacy callers (e.g. wifi.cpp Captive Portal) still get a
 * brief visual pause where the spinner used to live, without dragging
 * the 14 KB of skull frames back into the binary.
 */
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center) {
  (void)color; (void)x; (void)y; (void)center;
  delay((frameDelay > 0 ? frameDelay : 50) * (repeats > 0 ? repeats : 1));
}


/*
 * 
 * Display Logo
 * 
 */

// Hydra splash — draws the 1-bit logo bitmap centered, version line below.
// Bitmap is auto-trimmed in the converter so dimensions can vary as the
// artwork is retuned. PROGMEM size is whatever HYDRA_LOGO_W * HYDRA_LOGO_H
// packed to bytes is (currently ~4-5 KB).
void displayLogo(uint16_t color, int displayTime) {
  int16_t screenWidth = tft.width();
  int16_t screenHeight = tft.height();

  tft.fillScreen(TFT_BLACK);

  int16_t logoX = (screenWidth - HYDRA_LOGO_W) / 2;
  // Anchor near the top so a tall logo + version line still fits cleanly.
  int16_t logoY = 25;
  if (logoX < 0) logoX = 0;
  tft.drawBitmap(logoX, logoY, bitmap_hydra_logo, HYDRA_LOGO_W, HYDRA_LOGO_H, color);

  // Version line — size 2 so it's readable from across a room
  tft.setTextColor(color);
  tft.setTextFont(2);
  tft.setTextSize(1);
  const char* versionStr = "version: " HYDRA_VERSION;
  int16_t verW = tft.textWidth(versionStr, 2);
  int16_t verX = (screenWidth - verW) / 2;
  int16_t verY = logoY + HYDRA_LOGO_H + 14;
  if (verY + 20 > screenHeight) verY = screenHeight - 20;
  tft.setCursor(verX, verY);
  tft.print(versionStr);

  Serial.println("==================================");
  Serial.println("Hydra                             ");
  Serial.println("Developed by: Claude              ");
  Serial.print  ("Version:      "); Serial.println(HYDRA_VERSION);
  Serial.println("==================================");

  delay(displayTime);
}

// CC1101 GDO Hi-Z helper. NRF1 CE = GPIO 16 = CC1101 GDO2; NRF2 CE = GPIO 26
// = CC1101 GDO0. When CC1101 is active it drives those pins, fighting any
// NRF24 trying to use them as CE. Writing IOCFG2/IOCFG0 = 0x2E parks both
// outputs in Hi-Z so the NRF24s can own them. The writes go over GPIO 27
// (CC1101 CSN, shared with NRF2 CSN); NRF2 sees these as junk register
// writes but it isn't initialized yet so no harm.
void freeCC1101GdoPins() {
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  delayMicroseconds(100);
  digitalWrite(27, LOW);
  delayMicroseconds(20);
  SPI.transfer(0x00);  // W_REG IOCFG2
  SPI.transfer(0x2E);  // GDO2 -> Hi-Z, frees GPIO 16 (NRF1 CE)
  digitalWrite(27, HIGH);
  delayMicroseconds(50);
  digitalWrite(27, LOW);
  delayMicroseconds(20);
  SPI.transfer(0x02);  // W_REG IOCFG0
  SPI.transfer(0x2E);  // GDO0 -> Hi-Z, frees GPIO 26 (NRF2 CE)
  digitalWrite(27, HIGH);
  delayMicroseconds(50);
}


/*
 * 
 * Terminal
 * 
 */

namespace Terminal {

#define TEXT_HEIGHT 16
#define BOT_FIXED_AREA 0
#define TOP_FIXED_AREA 86
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define SCREEN_WIDTH 240
#define SCREENHEIGHT 320

static bool uiDrawn = false;

uint16_t yStart = TOP_FIXED_AREA;
uint16_t yArea = DISPLAY_HEIGHT - TOP_FIXED_AREA - BOT_FIXED_AREA;
uint16_t yDraw = DISPLAY_HEIGHT - BOT_FIXED_AREA - TEXT_HEIGHT;

uint16_t xPos = 0;

byte data = 0;

boolean change_colour = 1;
boolean selected = 1;
boolean terminalActive = true;

int blank[19];

long baudRates[] = {9600, 19200, 38400, 57600, 115200};
byte baudIndex = 0;

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 3 
    
    static int iconX[ICON_NUM] = {210, 170, 10}; 
    static int iconY = STATUS_BAR_Y_OFFSET;
    
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,    
        bitmap_icon_power,      
        bitmap_icon_go_back 
    };

    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
        
        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {  
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            } 
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;               
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;  
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                  if (terminalActive) {
                    terminalActive = false;
                  } else if (!terminalActive) {
                    baudIndex = (baudIndex + 1) % 5;
                    Serial.end();
                    delay(100);
                    Serial.begin(baudRates[baudIndex]);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
                    tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
                    delay(10);
                  }
                    break;
                case 1: 
                    delay(10);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    tft.drawCentreString(" Serial Terminal Active ", DISPLAY_WIDTH / 2, 37, 2);
                    terminalActive = true;
                    break;
  
                case 2: 
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();  
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50; 

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        if (ts.touched() && feature_active) { 
            TS_Point p = ts.getPoint();
            int x = ::map(p.x, 300, 3800, 0, SCREEN_WIDTH - 1);
            int y = ::map(p.y, 3800, 300, 0, SCREENHEIGHT - 1);

            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {
                            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                            animationState = 1;
                            activeIcon = i;
                            lastAnimationTime = millis();
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void scrollAddress(uint16_t vsp) {
  tft.writecommand(ILI9341_VSCRSADD);
  tft.writedata(vsp >> 8);
  tft.writedata(vsp);
}

int scroll_line() {
  int yTemp = yStart;
  tft.fillRect(0, yStart, blank[(yStart - TOP_FIXED_AREA) / TEXT_HEIGHT], TEXT_HEIGHT, TFT_BLACK);

  yStart += TEXT_HEIGHT;
  if (yStart >= DISPLAY_HEIGHT - BOT_FIXED_AREA) yStart = TOP_FIXED_AREA + (yStart - DISPLAY_HEIGHT + BOT_FIXED_AREA);
  scrollAddress(yStart);
  delay(1);
  return yTemp;
}

void setupScrollArea(uint16_t tfa, uint16_t bfa) {
  tft.writecommand(ILI9341_VSCRDEF);
  tft.writedata(tfa >> 8);
  tft.writedata(tfa);
  tft.writedata((DISPLAY_HEIGHT - tfa - bfa) >> 8);
  tft.writedata(DISPLAY_HEIGHT - tfa - bfa);
  tft.writedata(bfa >> 8);
  tft.writedata(bfa);
}

void terminalSetup() {

  setupTouchscreen();
  tft.fillScreen(TFT_BLACK); 

  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_WHITE);
  String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
  tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
  
  float currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, false);

  uiDrawn = false;

  Serial.begin(baudRates[baudIndex]);

  setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);

  for (byte i = 0; i < 19; i++) blank[i] = 0;

}

void terminalLoop() {
  
  runUI();

  if (terminalActive) {
    byte charCount = 0;
    while (Serial.available() && charCount < 10) {
      data = Serial.read();
      if (data == '\r' || xPos > 231) {
        xPos = 0;
        yDraw = scroll_line();
      }
      if (data > 31 && data < 128) {
        xPos += tft.drawChar(data, xPos, yDraw, 2);
        blank[(18 + (yStart - TOP_FIXED_AREA) / TEXT_HEIGHT) % 19] = xPos;
      }
      charCount++;
      }
    }
  }
}
