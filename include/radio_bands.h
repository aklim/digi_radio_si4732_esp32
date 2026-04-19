// ============================================================================
// radio_bands.h — Band taxonomy + band-table declarations.
//
// This header owns the data-only side of the radio module: the BandType /
// BandMode enums, the Band descriptor struct, and the declaration of the
// global band table. It is deliberately free of Arduino / SI4735 / FreeRTOS
// dependencies so the band table can be linked into host-native unit tests
// without pulling the whole radio stack.
//
// The definitions live in src/band_table.cpp. radio.h includes this header
// so every existing caller of Band / BandType / BandMode / g_bands continues
// to compile unchanged.
// ============================================================================

#ifndef RADIO_BANDS_H
#define RADIO_BANDS_H

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
//
// Frequency units are band-mode-dependent (Si4735 library convention):
//   MODE_FM       : uint16_t in 10 kHz units (e.g. 10240 == 102.40 MHz)
//   MODE_AM / LSB / USB : uint16_t in 1 kHz units (e.g. 1530 == 1530 kHz)
struct Band {
    const char* name;
    BandType    type;
    BandMode    mode;
    uint16_t    minFreq;        // native units (see above)
    uint16_t    maxFreq;
    uint16_t    defaultFreq;    // used on first boot / NVS schema-version reset
    uint16_t    currentFreq;    // last-tuned freq within this band
    uint16_t    step;           // native units per encoder detent
};

// Band table, defined in src/band_table.cpp. Index 0 is FM Broadcast, the
// default on first boot (before NVS has any stored band to restore).
//
// Thread-safety note: the pointer returned by radioGetCurrentBand() stays
// valid for the life of the program (the table lives in .data and is never
// reallocated). Individual fields may mutate (currentFreq is updated on
// every tune), so callers that need a stable snapshot of a field should
// copy it out immediately — or use radioGetFrequency() which goes through
// the mutex.
extern Band         g_bands[];
extern const size_t g_bandCount;

#endif  // RADIO_BANDS_H
