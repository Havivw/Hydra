/*
 * SubGHz raw RX capture (Flipper-compatible .sub writer)
 *
 * Companion to SubReplay. Lets the user listen on any WardriveConfig
 * channel, capture an OOK signal as timing samples, and save the result
 * to `/hydra/sub_files/rec_NNN.sub` in standard Flipper RAW format —
 * which SubReplay can play back immediately.
 *
 * Flow on entry:
 *   1. Frequency picker (UP/DOWN scroll, RIGHT confirm).
 *   2. Listening — CC1101 in OOK 650 async RX, ESP32 polls GDO0 for edges.
 *      Recording starts on first edge, stops on silence-after-activity or
 *      sample-buffer full.
 *   3. Review — shows sample count, asks UP=save / DOWN=discard.
 *   4. On save → next available `rec_NNN.sub` filename, written in Flipper
 *      RAW format. SubReplay's existing parser handles it unchanged.
 *
 * Press SELECT at any phase to exit back to the SubGHz submenu.
 */

#ifndef HYDRA_SUB_RECORD_H
#define HYDRA_SUB_RECORD_H

#include <Arduino.h>

namespace SubRecord {

void subRecordSetup();
void subRecordLoop();

}  // namespace SubRecord

#endif
