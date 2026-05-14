/*
 * NRF24 Promiscuous Sniffer — Hydra
 *
 * Listens with NRF24 in promiscuous mode (2-byte address 0xAA00, CRC off,
 * autoack off) and dumps every plausible packet's first bytes to the
 * screen. Different from the existing cifertech Scanner — that only
 * reports carrier-detect bool per channel; this surfaces actual payloads.
 *
 * Buttons:
 *   LEFT  / RIGHT  channel down / up (manual)
 *   UP             toggle auto-hop on/off
 *   SELECT         exit
 */

#ifndef NRF_SNIFFER_H
#define NRF_SNIFFER_H

#include <Arduino.h>

namespace NrfSniffer {

void sniffSetup();
void sniffLoop();

}  // namespace NrfSniffer

#endif
