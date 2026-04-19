// ============================================================================
// radio_format.h — Pure frequency display formatter (no mutex, no hardware).
//
// Split out of radio.cpp so the formatting logic can be exercised by native
// host unit tests without linking Arduino.h / SI4735.h / FreeRTOS. The
// thread-safe radioFormatFrequency() in radio.cpp snapshots the band mode +
// frequency under the radio mutex, then delegates the actual string build
// here.
//
// Units follow the Si4735 library convention (see radio_bands.h):
//   MODE_FM                 -> 10 kHz units, rendered as "NN.N MHz"
//   MODE_AM / LSB / USB     -> 1 kHz units, rendered as "NNNN kHz"
// ============================================================================

#ifndef RADIO_FORMAT_H
#define RADIO_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "radio_bands.h"

// Render `freq` (in `mode`'s native units) into `buf`. Writes at most
// `bufsize - 1` characters and always NUL-terminates. No-op if buf is
// null or bufsize < 2. Non-FM modes all share the kHz rendering (AM/MW/
// SW today; SSB when it lands).
static inline void radioFormatFrequencyPure(BandMode mode,
                                            uint16_t freq,
                                            char*    buf,
                                            size_t   bufsize) {
    if (!buf || bufsize < 2) return;

    if (mode == MODE_FM) {
        // Stored in 10 kHz units; render as MHz with one decimal.
        snprintf(buf, bufsize, "%u.%u MHz",
                 (unsigned)(freq / 100),
                 (unsigned)((freq % 100) / 10));
    } else {
        // Stored in 1 kHz units. MW/SW show whole kHz.
        snprintf(buf, bufsize, "%u kHz", (unsigned)freq);
    }
}

#endif  // RADIO_FORMAT_H
