/*
 * Hidden SSID Reveal — Hydra
 *
 * Discovers hidden WiFi networks (beacons with empty SSID IE) and tries to
 * reveal their real SSID by:
 *   1. Listening for Probe Responses from hidden BSSIDs (which carry the
 *      real SSID when triggered by a directed probe).
 *   2. Sniffing Association/Reassociation requests addressed to a hidden
 *      BSSID — clients put the real SSID in the IE list.
 *   3. Sniffing directed Probe Requests with a non-broadcast SSID — if the
 *      MAC matches a known client of a hidden BSSID, the probed SSID is
 *      likely the hidden one.
 *   4. Optionally periodically deauthing clients of hidden BSSIDs to force
 *      them to reconnect, which generates more reveal opportunities.
 */
#ifndef HIDDEN_REVEAL_H
#define HIDDEN_REVEAL_H

namespace HiddenReveal {
  void hiddenRevealSetup();
  void hiddenRevealLoop();
}

#endif
