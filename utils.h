#ifndef UTILS_H
#define UTILS_H

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>

extern TFT_eSPI tft;

#define XPT2046_IRQ   34
#define XPT2046_MOSI  32
#define XPT2046_MISO  35
#define XPT2046_CLK   25
#define XPT2046_CS    33

void updateStatusBar();
float readBatteryVoltage();
float readInternalTemperature();
bool isSDCardAvailable();
void drawStatusBar(float batteryVoltage, bool forceUpdate = false);

// CC1101 GDO Hi-Z helper — call before any NRF24 begin() so the CC1101 stops
// driving GPIO 16/26 (which are NRF1 CE and NRF2 CE on this board). Without
// this the NRF24 const carrier may look running but emit nothing because CE
// is being held by the CC1101's output drivers. Safe to call any time.
void freeCC1101GdoPins();

void initDisplay();
void showNotification(const char* title, const char* message);
void hideNotification();
void printWrappedText(int x, int y, int maxWidth, const char* text);
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center);
void displayLogo(uint16_t color, int displayTime);


namespace Terminal {
  void terminalSetup();
  void terminalLoop();
}

#endif
