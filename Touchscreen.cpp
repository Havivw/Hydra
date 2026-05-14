#include "Touchscreen.h"

// Touchscreen rides HSPI alongside the TFT (which is also HSPI per
// User_Setup.h #define USE_HSPI_PORT). TFT_eSPI drives HSPI through IO_MUX
// direct routing on pins 12/13/14/15; touchscreenSPI.begin() routes HSPI
// signals to 25/35/32/33 through the GPIO matrix. Output signals coexist
// (both pin sets receive the same clock/MOSI); MISO input is routed by
// the matrix from GPIO 35 (touch DOUT). TFT doesn't read MISO so the
// matrix override is harmless.
//
// This keeps VSPI exclusive to SD/NRF24/CC1101 so SD.begin() no longer
// steals touchscreen MISO — touch now survives SD use within a session.
SPIClass touchscreenSPI = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool feature_active = false;

void setupTouchscreen() {
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}
