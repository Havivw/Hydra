/*
 * Wardrive Channels — Settings submenu screen
 *
 * Scrollable list of every sub-GHz channel WardriveConfig exposes. User
 * navigates UP/DOWN, toggles selection with LEFT or RIGHT. SELECT exits.
 * RIGHT held with the cursor on the "ALL/NONE/DEFAULT" header row cycles
 * through the bulk-action presets.
 *
 * Selections are written straight into the WardriveConfig::channels[]
 * array — Wardrive feature picks them up next time it's opened.
 */

#ifndef WARDRIVE_CHANNELS_H
#define WARDRIVE_CHANNELS_H

#include <Arduino.h>

namespace WardriveChannels {

void wardriveChannelsSetup();
void wardriveChannelsLoop();

}  // namespace WardriveChannels

#endif
