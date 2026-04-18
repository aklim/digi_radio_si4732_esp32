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

// --- Band taxonomy ----------------------------------------------------------
// BandType groups bands by RF segment (used for future band-specific UI
// affordances, e.g. scale colour). BandMode decides which Si4735 setup
// function gets called (setFM / setAM / setSSB). Keeping the two split
// mirrors ATS-Mini's Common.h and leaves room for SSB on a band that is
// otherwise a plain AM SW segment.
enum BandType : uint8_t {
    BAND_FM,
    BAND_MW,
    BAND_SW,
    BAND_LW
};

enum BandMode : uint8_t {
    MODE_FM,
    MODE_AM,
    MODE_LSB,   // reserved for a future SSB-enabled PR
    MODE_USB    // reserved for a future SSB-enabled PR
};

// --- Band descriptor --------------------------------------------------------
// Mutable `currentFreq` tracks the last-tuned frequency within the band so
// switching bands + switching back restores the user's previous tune. Loaded
// from NVS at boot (see persist.cpp) and saved on change.
struct Band {
    const char* name;
    BandType    type;
    BandMode    mode;
    uint16_t    minFreq;        // native units (see header comment above)
    uint16_t    maxFreq;
    uint16_t    defaultFreq;    // used on first boot / NVS schema-version reset
    uint16_t    currentFreq;    // last-tuned freq within this band
    uint16_t    step;           // native units per encoder detent
};

// Band table, defined in radio.cpp. Index 0 is FM Broadcast, the default
// on first boot (before NVS has any stored band to restore).
//
// Thread-safety note: the pointer returned by radioGetCurrentBand() stays
// valid for the life of the program (the table lives in .data and is never
// reallocated). Individual fields may mutate (currentFreq is updated on
// every tune), so callers that need a stable snapshot of a field should
// copy it out immediately — or use radioGetFrequency() which goes through
// the mutex.
extern Band         g_bands[];
extern const size_t g_bandCount;

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

// --- Bandwidth filter -------------------------------------------------------
// Si4735 has distinct AM/SSB/FM filter tables. We keep two bandwidth
// catalogues (FM 5 presets, AM 7 presets) whose indices are _stable_ per
// mode; radio.cpp resolves the mode at call time.
//
// `idx` counts positions in the active-mode catalogue (0..count-1). Call
// radioGetBandwidthCount() / radioGetBandwidthDesc() to enumerate.
uint8_t     radioGetBandwidthIdx();
void        radioSetBandwidthIdx(uint8_t idx);
uint8_t     radioGetBandwidthCount();
const char* radioGetBandwidthDesc();       // active entry, e.g. "84k", "3.0k"
const char* radioGetBandwidthDescAt(uint8_t idx);

// --- AGC + attenuator -------------------------------------------------------
// The Si4735 packs AGC-enable and the manual attenuator into a single
// (AGCDIS, AGCIDX) pair. We expose the knob that user menus want: a
// single attIdx where 0 = AGC ON and 1..37 = AGC OFF + attenuation.
uint8_t radioGetAgcAttIdx();
void    radioSetAgcAttIdx(uint8_t idx);
bool    radioAgcIsOn();                    // convenience: attIdx == 0

#endif  // RADIO_H
