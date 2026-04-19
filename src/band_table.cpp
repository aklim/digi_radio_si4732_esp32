// ============================================================================
// band_table.cpp — Definitions for the radio band table.
//
// Split from radio.cpp so the table (and its invariants) can be linked into
// native host unit tests without pulling Arduino / SI4735 / FreeRTOS.
//
// FM Broadcast stays in 10 kHz units (Si4735 FM convention).
// AM / MW / SW stay in 1 kHz units (Si4735 AM convention).
// `step` is the encoder detent size in the band's native units.
//
// Band layout ported from ATS-Mini's Common.h + Menu.cpp
// (https://github.com/esp32-si4732/ats-mini). We start with four bands
// (FM Broadcast, MW, SW 41m, SW 31m); LW and additional SW segments plus
// SSB are tracked in docs/future_improvements.md. Index 0 is FM Broadcast
// so first-boot state (before NVS has a saved band) is identical to the v1
// single-band behaviour.
// ============================================================================

#include "radio_bands.h"

Band g_bands[] = {
    // FM Broadcast 87.0 – 108.0 MHz, 100 kHz steps. Matches v1 behaviour.
    { "FM Broadcast", BAND_FM, MODE_FM, 8700, 10800, 10240, 10240, 10 },
    // MW (AM broadcast) 520 – 1710 kHz, 10 kHz steps. Region-neutral (9 kHz
    // for ITU region 1 is a later PR behind a settings toggle).
    { "MW",           BAND_MW, MODE_AM,  520,  1710,  1000,  1000, 10 },
    // SW 41 m amateur/broadcast 7100 – 7300 kHz, 5 kHz steps.
    { "SW 41m",       BAND_SW, MODE_AM, 7100,  7300,  7200,  7200,  5 },
    // SW 31 m broadcast 9400 – 9900 kHz, 5 kHz steps.
    { "SW 31m",       BAND_SW, MODE_AM, 9400,  9900,  9700,  9700,  5 },
};

const size_t g_bandCount = sizeof(g_bands) / sizeof(g_bands[0]);
