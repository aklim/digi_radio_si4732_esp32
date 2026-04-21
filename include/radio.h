// ============================================================================
// radio.h — Si4732 multi-band receiver wrapper, dual-core-aware.
//
// All Si4732 state (the PU2CLR library instance, RDS mirror buffers, cached
// signal-quality values, the current band index) is owned by radio.cpp.
// main.cpp / menu.cpp / persist.cpp talk to the chip only through this
// header.
//
// Threading model:
//   - A dedicated FreeRTOS task (`radioTaskBody` in radio.cpp, started by
//     radioStart()) is pinned to Core 0 and drives the Si4735 I2C polling
//     on its own cadence (signal 500 ms, RDS 200 ms). The Arduino loop
//     task stays on Core 1 (default) and owns the UI + input.
//   - Every function in this header is thread-safe: internally each one
//     takes the radio module's mutex for the duration of its library call
//     or cached-state access. Callers pay a small lock/unlock cost on
//     each call; the common case is uncontended.
//   - radioPollSignal() / radioPollRds() no longer kick I2C themselves —
//     the task does that. They return "has the cached value changed since
//     the last call on this caller" so the UI's dirty-flag pipeline works
//     unchanged.
//   - radioSetBand / radioSetFrequency / radioSetVolume may briefly block
//     (<= ~20 ms worst case) if they collide with the task mid-poll.
//
// I2C bus: radioInit() assumes Wire.begin(...) has already been called by
// the caller — this keeps pin choice out of the radio module.
//
// Frequency units differ by band mode to match the PU2CLR library:
//   FM bands  : uint16_t in 10 kHz units (e.g. 10240 == 102.40 MHz)
//   AM / MW / SW / LW bands : uint16_t in 1 kHz units (e.g. 1530 == 1530 kHz)
// Callers should not hard-code FM's 10-kHz convention; use
// radioFormatFrequency() for display strings or multiply by
// radioGetCurrentBand()->step to compute encoder deltas.
// ============================================================================

#ifndef RADIO_H
#define RADIO_H

#include <stddef.h>
#include <stdint.h>

// Band taxonomy (BandType / BandMode enums), the Band descriptor struct,
// and the g_bands[] / g_bandCount declarations live in radio_bands.h so
// they can be linked into native host unit tests without pulling in
// Arduino / SI4735 / FreeRTOS. Re-exported here so every existing caller
// sees the same names from radio.h.
#include "radio_bands.h"

// --- FM broadcast band constants (back-compat aliases) ----------------------
// Retained so any call site that still references them compiles, but new code
// should go through radioGetCurrentBand() instead. These track g_bands[0]'s
// fields; if you renumber the band table they stay in sync via the constants
// defined alongside the table.
constexpr uint16_t FM_FREQ_MIN     = 8700;   // 87.0 MHz (10 kHz units)
constexpr uint16_t FM_FREQ_MAX     = 10800;  // 108.0 MHz
constexpr uint16_t FM_FREQ_DEFAULT = 10240;  // 102.4 MHz
constexpr uint16_t FM_FREQ_STEP    = 10;     // 100 kHz tuning step

// --- Volume -----------------------------------------------------------------
constexpr uint8_t DEFAULT_VOLUME = 30;
constexpr uint8_t MAX_VOLUME     = 63;

// --- Lifecycle --------------------------------------------------------------

// Power up the Si4732, create the radio mutex, and tune the chip to
// band[g_bandIdx] at its currentFreq. Does NOT start the radio task — call
// radioStart() after this completes so the task doesn't race with the
// caller's setup sequence.
void radioInit();

// Create and launch the radio polling task pinned to Core 0. Must be called
// exactly once, after radioInit() has returned. From this point on any
// radio.cpp function is safe to call from any task.
void radioStart();

// --- Band control -----------------------------------------------------------
// Switch the active band. Saves the outgoing band's currentFreq, reconfigures
// the Si4735 via setFM / setAM, re-applies volume, and returns. Callers that
// hold UI state (encoder bounds, header text, dirty flags) should refresh
// everything after this returns — nothing about the previous band's UI
// remains valid.
void radioSetBand(uint8_t idx);

uint8_t     radioGetBandIdx();
const Band* radioGetCurrentBand();

// --- Frequency / volume -----------------------------------------------------
// Frequency is always in the current band's native units (see header
// comment). Callers should clamp to minFreq / maxFreq themselves; the Si4735
// library also clamps but the local cache needs to match.
void     radioSetFrequency(uint16_t freq);
uint16_t radioGetFrequency();

// Volume: 0..MAX_VOLUME. Values above MAX_VOLUME are clamped.
void    radioSetVolume(uint8_t v);
uint8_t radioGetVolume();

// Render the current frequency as a short display string, e.g. "102.4 MHz"
// or "1530 kHz". Writes at most `bufsize - 1` characters. Chosen over a
// caller-side branch on bandMode so the UI never has to know units.
void radioFormatFrequency(char* buf, size_t bufsize);

// --- Signal / RDS -----------------------------------------------------------
// Drain the "signal changed" flag set by the radio task. Returns true once
// after a new RSSI / SNR / stereo sample arrived and clears the flag.
// Callers should treat this as "should I repaint the signal UI now?".
bool    radioPollSignal();
uint8_t radioGetRssi();       // 0..127 dBuV
uint8_t radioGetSnr();        // dB
bool    radioIsStereo();      // pilot bit; always false on AM bands

// Drain the "RDS changed" flag set by the radio task. Returns true once
// after new PS / RT data arrived (or existing text was cleared after
// 10 s without sync). No-op semantics on non-FM bands.
bool radioPollRds();

// Copy the current PS (up to 8 chars + NUL) / RT (up to 64 chars + NUL)
// into the caller's buffer. Both getters copy under the radio mutex so
// the UI can call them from Core 1 while the radio task (Core 0) may be
// writing. Empty string on no-sync, stale, or AM band. `bufsize` must be
// at least 1; the function always NUL-terminates and writes at most
// `bufsize-1` characters.
void radioGetRdsPs(char* buf, size_t bufsize);
void radioGetRdsRt(char* buf, size_t bufsize);

// RDS Programme Identification code (4-hex-digit station ID). Returns 0
// when not decoded yet, on no-sync, or on non-FM bands.
uint16_t radioGetRdsPi();

// User-controlled RDS decode gate. When disabled, the Core-0 task's RDS
// poll early-returns (no I²C traffic), the Si4735's RDS block is turned
// off via setRdsConfig(0,...), and the PS/RT/PI mirrors are cleared so
// the UI falls back to the band scale. Enabling on an FM band re-arms
// the library and a fresh sync re-populates the mirror within a second.
// Safe to call from any thread — mutex-guarded internally.
void radioSetRdsEnabled(bool enabled);
bool radioGetRdsEnabled();

// True when RDS is enabled AND the chip currently reports in-sync RDS
// (i.e. g_lastRdsSync is fresh within RDS_STALE_MS). Drives the header
// indicator's dim/bright colour split.
bool radioIsRdsSyncing();

// --- Bandwidth filter -------------------------------------------------------
// Si4735 has distinct AM/SSB/FM filter tables. We keep two bandwidth
// catalogues (FM 5 presets, AM 7 presets) whose indices are _stable_ per
// mode; radio.cpp resolves the mode at call time. The shadow for the
// inactive mode survives across band switches, so FM keeps its filter
// independently of AM/SW.
//
// `idx` counts positions in the active-mode catalogue (0..count-1). Call
// radioGetBandwidthCount() / radioGetBandwidthDesc() to enumerate.
uint8_t     radioGetBandwidthIdx();
void        radioSetBandwidthIdx(uint8_t idx);
uint8_t     radioGetBandwidthCount();
const char* radioGetBandwidthDesc();       // active entry, e.g. "84k", "3.0k"
const char* radioGetBandwidthDescAt(uint8_t idx);

// Seed the FM / AM bandwidth shadow without touching the chip. Intended for
// main.cpp boot: persist.cpp's loaded values are pushed in before radioInit
// so the first applyBandLocked() picks them up.
void radioSeedBandwidthIdx(bool fm, uint8_t idx);

// --- AGC + attenuator -------------------------------------------------------
// The Si4735 packs AGC-enable and the manual attenuator into a single
// (AGCDIS, AGCIDX) pair. We expose a single per-mode idx:
//   idx == 0       -> AGC enabled (AGCDIS=0)
//   idx == 1       -> AGC disabled, AGCIDX = 0 (no attenuation)
//   idx >= 1       -> AGC disabled, AGCIDX = idx - 1
// Semantics match ATS-Mini's doAgc (Menu.cpp). Per-mode ranges:
//   FM: 0..27  (28 entries, one "AGC on" row + 27 manual-attenuator rows)
//   AM: 0..37  (38 entries)
// The FM and AM indices are tracked independently — switching bands
// between modes doesn't clobber the other mode's setting.
uint8_t radioGetAgcAttIdx();
uint8_t radioGetAgcAttMax();               // max idx for the active mode
void    radioSetAgcAttIdx(uint8_t idx);
bool    radioAgcIsOn();                    // convenience: attIdx == 0

// Seed the FM / AM AGC shadow without touching the chip. Boot-time twin of
// radioSeedBandwidthIdx.
void radioSeedAgcIdx(bool fm, uint8_t idx);

// Re-apply the active band's full setup (setFM/setAM + BW + AGC + volume).
// Used at boot so the chip picks up BW/AGC values seeded from NVS *after*
// radioInit()'s initial apply pass. radioSetBand() calls this transitively
// whenever the user switches bands.
void radioApplyCurrentBand();

// --- Low-level hooks for the bandscope sweep -------------------------------
// These expose just enough of the SI4735 for Scan.cpp to retune the chip
// point-by-point without needing the private g_radio handle. All three
// take the radio mutex internally.
//
// radioScanEnter() suspends the Core-0 polling task's RSSI / RDS work (so
// it doesn't compete with the sweep) and mutes audio. radioScanExit()
// tunes back to restoreFreq and resumes normal polling.
void radioScanEnter();
void radioScanExit(uint16_t restoreFreq);

// Retune to `freq` (native band units) and return the signal-quality
// readings. Caller should be inside the scanEnter / scanExit window.
// `settleMs` is the delay between setFrequency() and the RSSI/SNR read
// (upstream uses 30 ms, 60 ms FM, 80 ms AM; pass what you like).
void radioScanMeasure(uint16_t freq, uint16_t settleMs,
                      uint8_t &outRssi, uint8_t &outSnr);

#endif  // RADIO_H
