// ============================================================================
// rds_sanitize.h — Pure RDS RadioText sanitiser.
//
// Copy the PU2CLR library's RadioText buffer into `dst` (typically 65 bytes),
// dropping leading whitespace, folding mid-buffer control / high bytes to
// spaces, and right-trimming the result. Returns true iff the cleaned
// string contains at least one printable non-space character.
//
// Why this is its own unit (and not a static helper in radio.cpp): the
// library's rds_buffer2A[] can carry garbage for up to three 32-ms RDS
// blocks before a fresh 2A group fully assembles, and on weak signal can
// contain all-zero or all-control-char runs. Letting the UI decide whether
// to render it led to occasional blank / non-printable flashes in the RT
// row (v2.1.0 fixed most cases via a Layout-Default scan, but stations
// that drip a single stray 0x2F-lookalike into the buffer slipped through).
// Gating at the source is the only way to guarantee "if g_rt is non-empty,
// it's real printable text".
//
// Strategy matches ATS-Mini's showRadioText (Station.cpp:123-165):
//   - skip leading whitespace,
//   - copy up to 64 bytes, replacing non-printables with ' ',
//   - right-trim trailing whitespace,
//   - NUL-terminate.
//
// Split out of radio.cpp so it can be exercised by native host unit tests
// without linking Arduino.h / SI4735.h / FreeRTOS.
// ============================================================================

#ifndef RDS_SANITIZE_H
#define RDS_SANITIZE_H

#include <stddef.h>

bool rdsSanitizeRt(const char* src, char* dst, size_t dstSize);

#endif  // RDS_SANITIZE_H
