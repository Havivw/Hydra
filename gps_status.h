/*
 * GPS Status — Hydra
 *
 * Live diagnostic screen for the UART GPS module. Shows fix state,
 * satellite count, lat/lon, UTC time, NMEA sentence rate. Run this first
 * after wiring up a GPS module to confirm everything is talking before
 * relying on GPS in Wardrive / .sub-file features.
 *
 * SELECT exits.
 */

#ifndef GPS_STATUS_H
#define GPS_STATUS_H

#include <Arduino.h>

namespace GpsStatus {

void gpsStatusSetup();
void gpsStatusLoop();

}  // namespace GpsStatus

#endif
