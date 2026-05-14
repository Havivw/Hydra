/*
 * Hydra GPS subsystem
 *
 * Reads NMEA from a UART GPS module (NEO-6M / 7M / 8M / ATGM336H) on
 * HardwareSerial(2). Pins picked to be reachable from the DIV v1 shield's
 * exposed 10x2 header (P1/P2) and to not steal the touchscreen lines:
 *
 *   GPS module TX → ESP32 GPIO 17  (data flows GPS → ESP32 here)
 *                   == header pin labelled "IO17"
 *                   == NRF24 #3 CSN — only conflicts if NRF3 features run
 *   GPS module RX → ESP32 GPIO 4   (only used to send config commands)
 *                   == header pin labelled "IO4"
 *                   == TFT backlight pin AND NRF24 #1 CE; the firmware
 *                      drives the backlight HIGH at boot and never toggles
 *                      it, so steady-state coexistence works. Avoid
 *                      sending GPS config commands while NRF1 is active.
 *
 * Earlier revisions of this file used GPIO 32/25, which are the XPT2046
 * touchscreen's CLK and MOSI lines — calling Gps::begin() permanently
 * broke touch for the rest of the session. Those pins are also NOT on
 * the exposed shield header, so wiring a module there required soldering
 * directly to the ESP32 module. The header-accessible pins above are
 * the supported wiring as of Hydra v0.0.3.
 *
 * The module has its own internal state — call Gps::begin() once at boot
 * (or first use), then Gps::update() in any loop where you want fresh
 * data. State persists across feature launches: once a fix is acquired,
 * it stays available to every feature that asks (Wardrive logger, etc.).
 *
 * Uses MicroNMEA — already in the Hydra sandbox (Marauder dep). Buffer is
 * static-sized so no malloc.
 */

#ifndef HYDRA_GPS_H
#define HYDRA_GPS_H

#include <Arduino.h>

namespace Gps {

// Default wiring on the DIV v1 shield header (P1/P2)
#define HYDRA_GPS_UART_RX 17   // ESP32 pin connected to GPS module TX
#define HYDRA_GPS_UART_TX 4    // ESP32 pin connected to GPS module RX
#define HYDRA_GPS_BAUD    9600

// Initialise the UART + NMEA parser. Safe to call repeatedly; subsequent
// calls are no-ops. Returns true if the UART came up.
bool begin();

// Drain whatever's pending on the UART and feed it to the NMEA parser.
// Call this from any feature loop that wants fresh data; cheap when idle.
void update();

// True once the parser has seen a valid fix in the last few seconds.
bool hasFix();

// Latitude / Longitude in millionths of a degree (MicroNMEA native format),
// or 0 when no fix. Convert to degrees by dividing by 1e6.
long latitudeMillionths();
long longitudeMillionths();

// Convenience: latitude/longitude as float degrees. NAN when no fix.
float latitude();
float longitude();

// Number of satellites used in current solution. 0 when no fix.
uint8_t satCount();

// UTC time as "HH:MM:SS" / date as "YYYY-MM-DD". Both return "--:--:--" /
// "----------" when no fix yet.
const char* timeStr();
const char* dateStr();

// Total NMEA sentences successfully parsed since begin(). Useful as a "we
// are at least getting bytes from the module" indicator on the status UI.
uint32_t sentenceCount();

}  // namespace Gps

#endif
