/*
 * ESPPwnagotchi — combined deauth/CSA + WPA handshake EAPOL capture
 *
 * Iterates the selected AP list (TargetList::targets[].selected). For each
 * target, runs a 3-phase cycle on that AP's channel:
 *   1. DEAUTH burst   — broadcast deauth frames at the BSSID (kicks clients)
 *   2. CSA burst      — spoofed channel-switch beacons (also forces re-auth)
 *   3. LISTEN window  — promiscuous sniff for 802.11 Data frames carrying
 *                       EAPOL ethertype 0x88 0x8E. Captured frames are
 *                       written to /handshakes/cap_NNNN.pcap on the SD card
 *                       in LINKTYPE_IEEE802_11 (105) format — tcpdump,
 *                       Wireshark, aircrack-ng all read this directly.
 *
 * The 4-way handshake (M1-M4) is the high-value bit. We capture every EAPOL
 * frame we see and let the analyst sort it out offline.
 */

#ifndef ESPPWNAGOTCHI_H
#define ESPPWNAGOTCHI_H

#include <Arduino.h>

namespace EspPwnagotchi {

void pwnSetup();
void pwnLoop();

}  // namespace EspPwnagotchi

#endif
