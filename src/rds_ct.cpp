// ============================================================================
// rds_ct.cpp — RDS Clock-Time helpers (see rds_ct.h for contract).
//
// Split from radio.cpp so the logic can be exercised by native host unit
// tests without pulling Arduino.h / SI4735.h. No Arduino-only APIs here.
// ============================================================================

#include "rds_ct.h"

bool rdsCtHmIsValid(uint16_t hour, uint16_t minute) {
    return hour <= 23 && minute <= 59;
}

size_t rdsCtFormatHM(uint16_t hour, uint16_t minute, char* out, size_t bufsize) {
    if (!out || bufsize < 6) {
        if (out && bufsize) out[0] = 0;
        return 0;
    }
    // Clamp defensively — callers should have passed rdsCtHmIsValid() first,
    // but better to print "99:99" than to smear a three-digit glyph into
    // the next sidebar row.
    if (hour   > 99) hour   = 99;
    if (minute > 99) minute = 99;
    out[0] = (char)('0' + ((hour   / 10) % 10));
    out[1] = (char)('0' +  (hour        % 10));
    out[2] = ':';
    out[3] = (char)('0' + ((minute / 10) % 10));
    out[4] = (char)('0' +  (minute       % 10));
    out[5] = 0;
    return 5;
}
