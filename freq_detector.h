/*
 * CC1101 Frequency Detector — Hydra
 *
 * Sweeps the channels selected in WardriveConfig and maintains a
 * rolling-peak RSSI per channel. Live display shows the top-N strongest
 * channels sorted by recent RSSI — "what's loudest right now".
 *
 * Different from Wardrive: no logging, no GPS, just live identification of
 * the active frequency. Useful for finding the freq of an unknown
 * transmitter (e.g. an unmarked keyfob, garage opener).
 *
 * SELECT exits.
 */

#ifndef FREQ_DETECTOR_H
#define FREQ_DETECTOR_H

#include <Arduino.h>

namespace FreqDetector {

void freqDetectorSetup();
void freqDetectorLoop();

}  // namespace FreqDetector

#endif
