/*
 * Hydra GPS subsystem — see header.
 */

#include "gps.h"

#include <HardwareSerial.h>
#include <MicroNMEA.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Gps {

static HardwareSerial gpsSerial(2);  // UART2

static char nmeaBuffer[120];
static MicroNMEA nmea(nmeaBuffer, sizeof(nmeaBuffer));

static bool gpsInitialised = false;
static uint32_t totalSentences = 0;

// Buffers for the string accessors — refreshed lazily.
static char timeBuf[10] = "--:--:--";
static char dateBuf[12] = "----------";

bool begin() {
  if (gpsInitialised) return true;
  gpsSerial.begin(HYDRA_GPS_BAUD, SERIAL_8N1, HYDRA_GPS_UART_RX, HYDRA_GPS_UART_TX);
  gpsInitialised = true;
  return true;
}

void update() {
  if (!gpsInitialised) return;
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    if (nmea.process(c)) {
      totalSentences++;
    }
  }
}

bool hasFix() {
  if (!gpsInitialised) return false;
  return nmea.isValid();
}

long latitudeMillionths() {
  if (!hasFix()) return 0;
  return nmea.getLatitude();
}

long longitudeMillionths() {
  if (!hasFix()) return 0;
  return nmea.getLongitude();
}

float latitude() {
  if (!hasFix()) return NAN;
  return nmea.getLatitude() / 1e6f;
}

float longitude() {
  if (!hasFix()) return NAN;
  return nmea.getLongitude() / 1e6f;
}

uint8_t satCount() {
  if (!gpsInitialised) return 0;
  return nmea.getNumSatellites();
}

const char* timeStr() {
  if (!gpsInitialised || !nmea.isValid()) {
    strcpy(timeBuf, "--:--:--");
    return timeBuf;
  }
  snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u",
           nmea.getHour(), nmea.getMinute(), nmea.getSecond());
  return timeBuf;
}

const char* dateStr() {
  if (!gpsInitialised || nmea.getYear() == 0) {
    strcpy(dateBuf, "----------");
    return dateBuf;
  }
  snprintf(dateBuf, sizeof(dateBuf), "%04u-%02u-%02u",
           nmea.getYear(), nmea.getMonth(), nmea.getDay());
  return dateBuf;
}

uint32_t sentenceCount() {
  return totalSentences;
}

}  // namespace Gps
