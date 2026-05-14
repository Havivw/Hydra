/*
 * Evil Portal + Deauth combo — open rogue AP that mirrors the SSID of the
 * first selected target, while continuously deauthing the real AP. Goal: get
 * clients to roam onto our AP. Pair with the existing Captive Portal app
 * (run this first, then Captive Portal) for the full credential-capture flow.
 *
 * UP toggles attack. Open AP stays up while attack runs. SELECT exits.
 */

#ifndef EVIL_DEAUTH_H
#define EVIL_DEAUTH_H

#include <Arduino.h>

namespace EvilDeauth {

void evilDeauthSetup();
void evilDeauthLoop();

}  // namespace EvilDeauth

#endif
