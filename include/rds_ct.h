// ============================================================================
// rds_ct.h — Pure RDS Clock-Time helpers.
//
// RDS group 4A carries a Modified-Julian-Day date plus hour, minute and a
// Local Time Offset. PU2CLR decodes the group and exposes the local time
// via `SI4735::getRdsDateTime(uint16_t *y, uint16_t *m, uint16_t *d,
// uint16_t *h, uint16_t *min)`. This module owns the two pieces of logic
// around that raw output:
//
//   1. rdsCtHmIsValid() — HH:MM range check. PU2CLR has been observed
//      returning true with h > 23 / mi > 59 on weak signal; the UI
//      never sees out-of-range values. Date components (y/mo/d) are
//      deliberately NOT validated here: observed traffic from Ukrainian
//      FM stations lands years in 1906/1907 because of a PU2CLR MJD
//      underflow after LTO correction, but the HH:MM values are still
//      correct. Adding a date row is a future enhancement that would
//      bring its own validator.
//   2. rdsCtFormatHM() — zero-padded "HH:MM" formatter. Hand-assembled
//      instead of snprintf to avoid pulling the stdio float formatter
//      into the firmware binary.
//
// Split out of radio.cpp so the native test env can exercise both helpers
// without Arduino / SI4735 / FreeRTOS.
// ============================================================================

#ifndef RDS_CT_H
#define RDS_CT_H

#include <stddef.h>
#include <stdint.h>

// True iff hour <= 23 AND minute <= 59. Trivial body, but named so the
// intent is explicit at the call site and the contract is testable.
bool rdsCtHmIsValid(uint16_t hour, uint16_t minute);

// Format "HH:MM" into `out`. `bufsize` must be >= 6 (5 chars + NUL).
// Returns the number of bytes written excluding the terminator (5 on
// success, 0 when bufsize is too small or out is NULL).
size_t rdsCtFormatHM(uint16_t hour, uint16_t minute, char* out, size_t bufsize);

#endif  // RDS_CT_H
