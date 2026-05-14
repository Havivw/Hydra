/*
 * CC1101 Spectrum Analyzer — Hydra
 *
 * Sweeps through the channels selected in WardriveConfig and draws each
 * one's current RSSI as a vertical bar on the TFT. The display is a
 * rolling-update real-time view: every full sweep redraws the bars.
 *
 * Useful for quickly seeing which sub-GHz bands are active. Same channel
 * selection as Wardrive — change it in Settings → Wardrive Channels.
 *
 * SELECT exits.
 */

#ifndef SPECTRUM_ANALYZER_H
#define SPECTRUM_ANALYZER_H

#include <Arduino.h>

namespace SpectrumAnalyzer {

void spectrumSetup();
void spectrumLoop();

}  // namespace SpectrumAnalyzer

#endif
