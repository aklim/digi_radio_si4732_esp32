// ============================================================================
// persist.h — versioned NVS (Preferences) wrapper for the radio's runtime
//             state: current band, per-band last tuned frequency, volume.
//
// Modelled on ATS-Mini's schema-versioning pattern (VER_SETTINGS / VER_BANDS
// in ats-mini/Common.h): a single PERSIST_SCHEMA_VER guards the stored
// namespace so a firmware upgrade that changes the meaning of any key can
// wipe stale state cleanly instead of mis-interpreting it.
//
// Writes are rate-limited inside persist.cpp (≥1 s between flushes for a
// given key) so rapid encoder rotation does not hammer the SPI-flash NVS
// partition. Callers can poke persistSaveFrequency() every detent without
// worrying about wear.
//
// Namespace: "radio" (single consolidated namespace; a future settings PR
// can add a second namespace for UI state without migrating this one).
// ============================================================================

#ifndef PERSIST_H
#define PERSIST_H

#include <stdint.h>

// Bump this when the meaning of any persisted key changes. On boot, if the
// stored value doesn't match, persistInit() wipes the namespace and
// initialises defaults — callers get first-boot behaviour without a separate
// reset path.
constexpr uint16_t PERSIST_SCHEMA_VER = 1;

// Load cached values from NVS and apply the schema-version gate. Safe to
// call before radioInit(); it does not touch the Si4735. Idempotent.
void persistInit();

// Flush any pending rate-limited writes (call before deep sleep / reboot).
// In normal operation persist.cpp flushes on its own timer; this entry point
// is here for completeness and for future sleep support.
void persistFlush();

// --- Current band ----------------------------------------------------------
uint8_t persistLoadBand();
void    persistSaveBand(uint8_t idx);

// --- Per-band last-tuned frequency -----------------------------------------
// Stored per band index (key: "freq<idx>"). `freq` is in the band's native
// units — callers don't need to know the unit; persist just roundtrips the
// integer. `freq == 0` is treated as "no saved value" and falls back to the
// band's defaultFreq.
uint16_t persistLoadFrequency(uint8_t bandIdx);
void     persistSaveFrequency(uint8_t bandIdx, uint16_t freq);

// --- Global volume ---------------------------------------------------------
uint8_t persistLoadVolume();
void    persistSaveVolume(uint8_t vol);

#endif  // PERSIST_H
