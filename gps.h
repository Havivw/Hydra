/*
 * Hydra GPS subsystem
 *
 * Reads NMEA from a UART GPS module (NEO-6M / 7M / 8M) on HardwareSerial(2).
 * Default wiring on the DIV v1 shield header:
 *   GPS module TX → ESP32 GPIO 32  (data flows GPS → ESP32 here)
 *   GPS module RX → ESP32 GPIO 25  (only used if we ever send config commands)
 *
 * The module has its own internal state — call Gps::begin() once at boot
 * (or first use), then Gps::update() in any loop where you want fresh data.
 * State persists across feature launches: once a fix is acquired, it stays
 * available to every feature that asks (Wardrive logger, etc.).
 *
 * Uses MicroNMEA — already in the Hydra sandbox (Marauder dep). Buffer is
 * static-sized so no malloc.
 */

#ifndef HYDRA_GPS_H
#define HYDRA_GPS_H

#include <Arduino.h>

namespace Gps {

// Default wiring on the DIV v1 shield header
#define HYDRA_GPS_UART_RX 32   // ESP32 pin connected to GPS module TX
#define HYDRA_GPS_UART_TX 25   // ESP32 pin connected to GPS module RX
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
