/*
 * NRF24 Mousejack Scanner — Hydra
 *
 * Passive detector for Logitech Unifying / Microsoft Lightning Bolt 2.4 GHz
 * wireless mouse/keyboard devices vulnerable to the 2016 Mousejack class
 * of CVEs (CVE-2016-3346 et al.). Uses the well-known NRF24L01+
 * promiscuous-mode trick: set the receive address to 0x00AA (2 bytes),
 * disable CRC + auto-ack, then sweep channels watching for preamble
 * matches and Logitech-shaped packets.
 *
 * Not an exploit — this only LISTENS and reports which devices/channels
 * are present. Useful for surveying wireless peripherals nearby.
 *
 * SELECT exits.
 */

#ifndef NRF_MOUSEJACK_H
#define NRF_MOUSEJACK_H

#include <Arduino.h>

namespace NrfMousejack {

void mousejackSetup();
void mousejackLoop();

}  // namespace NrfMousejack

#endif
