/*
 * Flock Detector — Hydra port
 *
 * Passive 2.4 GHz promiscuous-mode scanner for Flock Safety surveillance
 * infrastructure. Ported into Hydra from colonelpanichacks/flock-you
 * (https://github.com/colonelpanichacks/flock-you).
 *
 * OUI research credit: ØяĐöØцяöЪöяцฐ / @NitekryDPaul (30 OUIs +
 * addr1-receiver strategy) and Michael / DeFlockJoplin (31st OUI +
 * wildcard-probe-request signature).
 *
 * What's lifted vs the upstream:
 *   - The full 31-OUI target list
 *   - matchOuiRaw + isMulticast filters (locally-administered MAC skip)
 *   - Promiscuous sniffer callback (addr1/addr2/addr3 OUI match)
 *   - Wildcard probe-request signature (tag 0 length 0 from known-OUI addr2)
 *   - Channel hop 1/6/11 with 350ms dwell
 *
 * What's NOT ported (and why):
 *   - SPIFFS persistence — DIV v1 splits SPIFFS between cifertech features
 *     already; out of scope for first POC.
 *   - GPS tagging — DIV v1 has no GPS (see plans/01-hardware-divv1.md).
 *   - Piezo buzzer + LED — DIV v1 has no buzzer pin we can drive (GPIO 36 is
 *     ADC-input-only) and no identified NeoPixel pin.
 *   - Flask dashboard JSON over USB — Serial log only for now.
 */

#ifndef FLOCK_DETECT_H
#define FLOCK_DETECT_H

#include <Arduino.h>

namespace FlockDetect {

void flockDetectSetup();
void flockDetectLoop();

}  // namespace FlockDetect

#endif  // FLOCK_DETECT_H
