// ============================================================================
// rds_sanitize.cpp — RDS RadioText sanitiser (see rds_sanitize.h for contract).
//
// Split from radio.cpp so the logic can be exercised by native host unit
// tests without pulling Arduino.h / SI4735.h. No Arduino-only APIs here.
// ============================================================================

#include "rds_sanitize.h"

#include <stddef.h>
#include <stdint.h>

bool rdsSanitizeRt(const char* src, char* dst, size_t dstSize) {
    if (!src || !dst || dstSize < 2) return false;
    // Skip leading whitespace (ASCII <= 0x20) up to the first 64 bytes.
    size_t i = 0;
    while (i < 64 && src[i] && (uint8_t)src[i] <= ' ') i++;

    size_t j = 0;
    size_t limit = dstSize - 1;
    bool anyPrintable = false;
    for (; i < 64 && src[i] && j < limit; i++) {
        uint8_t c = (uint8_t)src[i];
        // CR/LF terminate the text (matches PU2CLR library convention).
        if (c == 0x0D || c == 0x0A) break;
        if (c < 0x20 || c > 0x7E) {
            dst[j++] = ' ';
        } else {
            dst[j++] = (char)c;
            if (c != ' ') anyPrintable = true;
        }
    }
    // Right-trim trailing whitespace.
    while (j > 0 && dst[j - 1] == ' ') j--;
    dst[j] = 0;
    return anyPrintable && j > 0;
}
