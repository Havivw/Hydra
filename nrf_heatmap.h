/*
 * NRF24 Channel Heatmap — Hydra
 *
 * Sweeps all 128 NRF24 channels (2400-2527 MHz) using the chip's carrier
 * detect (RPD register). Maintains a *persistent* per-channel activity
 * score that decays slowly over a ~60 second window, so a channel that
 * had a brief burst stays visible after the carrier is gone. Different
 * from cifertech's Scanner, which only shows the current instant.
 *
 * Useful for finding intermittent transmitters (key fobs that ping
 * occasionally, devices in low-duty-cycle modes).
 *
 * SELECT exits.
 */

#ifndef NRF_HEATMAP_H
#define NRF_HEATMAP_H

#include <Arduino.h>

namespace NrfHeatmap {

void heatmapSetup();
void heatmapLoop();

}  // namespace NrfHeatmap

#endif
