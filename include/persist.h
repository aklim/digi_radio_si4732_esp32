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

// Bump this when the meaning of any persisted key changes. persistInit()
// either applies a lazy upgrade (additive-only, keeps existing values) or
// wipes and resets to defaults on anything else.
//
// Version history:
//   v1 — initial: band, vol, freq<N>
//   v2 — adds: theme (active palette index, see Themes.h)
//   v3 — adds: bw_fm, bw_am (Si4732 IF-filter index per mode),
//              agc_fm, agc_am (AGC / manual attenuator index per mode)
//   v4 — adds: rds_en (RDS decode enable, default 1),
//              bt_en  (Bluetooth enable, default 0),
//              wifi_en (WiFi enable, default 0)
constexpr uint16_t PERSIST_SCHEMA_VER = 4;

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

// --- Active UI theme (Themes.h catalogue index) ----------------------------
uint8_t persistLoadTheme();
void    persistSaveTheme(uint8_t idx);

// --- Per-mode Si4732 IF filter (bandwidth) index ---------------------------
// FM: 0..4 from kBwFm[] (Auto / 110k / 84k / 60k / 40k), default 0 (Auto).
// AM/MW/SW: 0..6 from kBwAm[] (1.0k / 1.8k / 2.0k / 2.5k / 3.0k / 4.0k / 6.0k),
// default 4 (3.0k). See radio.cpp for the catalogue definitions.
uint8_t persistLoadBandwidthFm();
void    persistSaveBandwidthFm(uint8_t idx);
uint8_t persistLoadBandwidthAm();
void    persistSaveBandwidthAm(uint8_t idx);

// --- Per-mode AGC / manual attenuator index --------------------------------
// Index 0 = AGC enabled. Index 1..N = AGC disabled + attenuator (index-1).
// FM range: 0..27. AM range: 0..37. Matches ATS-Mini semantics so the
// sidebar label ("Att NN") and the chip's AGCIDX register agree.
uint8_t persistLoadAgcFm();
void    persistSaveAgcFm(uint8_t idx);
uint8_t persistLoadAgcAm();
void    persistSaveAgcAm(uint8_t idx);

// --- Feature enable flags (v4) ---------------------------------------------
// Stored as u8 (0/1). Defaults: RDS on, Bluetooth off, WiFi off.
// RDS-off short-circuits the I²C poll loop in radio.cpp; BT/WiFi flags are
// consumed by connectivity.cpp (the real stacks are not implemented yet —
// flags currently only gate the header indicator icons).
uint8_t persistLoadRdsEnabled();
void    persistSaveRdsEnabled(uint8_t en);
uint8_t persistLoadBtEnabled();
void    persistSaveBtEnabled(uint8_t en);
uint8_t persistLoadWifiEnabled();
void    persistSaveWifiEnabled(uint8_t en);

#endif  // PERSIST_H
