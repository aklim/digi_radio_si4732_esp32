// ============================================================================
// radio.h — Si4732 multi-band receiver wrapper.
//
// All Si4732 state (the PU2CLR library instance, RDS mirror buffers, cached
// signal-quality values, the current band index) is owned by radio.cpp.
// main.cpp / menu.cpp / persist.cpp talk to the chip only through this
// header.
//
// Threading: not thread-safe. All functions must be called from the Arduino
// loop task (same core that drives the I2C peripheral). The dual-core split
// Berndt uses in pocketSI4735DualCoreDecoder will add proper synchronization
// in a later PR; for now this module assumes a single caller.
//
// I2C bus: radioInit() assumes Wire.begin(...) has already been called by the
// caller — this keeps pin choice out of the radio module.
//
// Rate limiting: radioPollSignal() and radioPollRds() self-rate-limit; they
// can safely be called every loop iteration. RDS polling is a no-op on
// non-FM bands (the chip doesn't decode RDS on AM / MW / SW / LW).
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

// Power up the Si4732 and tune it to the band previously saved in NVS (or
// index 0 / FM Broadcast on a fresh device). The chip must already be out of
// hardware reset (RC circuit). I2C (Wire) must already be initialised.
void radioInit();

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
// Poll RSSI / SNR / stereo pilot. Self-rate-limited to 500 ms; callers can
// invoke this every loop iteration. Returns true when any cached value
// changed during this call — useful as a dirty-flag trigger for the UI.
bool    radioPollSignal();
uint8_t radioGetRssi();       // 0..127 dBuV
uint8_t radioGetSnr();        // dB
bool    radioIsStereo();      // pilot bit; always false on AM bands

// Poll RDS. Self-rate-limited to 200 ms. Returns true when the local PS or RT
// mirror changed. No-op on non-FM bands (returns false without touching I2C).
bool        radioPollRds();
const char* radioGetRdsPs();  // up to 8 chars, "" when no sync / stale / AM
const char* radioGetRdsRt();  // up to 64 chars, "" when no sync / stale / AM

#endif  // RADIO_H
