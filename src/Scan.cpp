// ============================================================================
// Scan.cpp — bandscope sweep engine.
//
// Mirror of ats-mini/Scan.cpp adapted to our radio.cpp facade. The upstream
// code talks to the SI4735 library directly; here we go through
// radioScanEnter / radioScanMeasure / radioScanExit so Core-0 polling
// stays coordinated.
//
// Data model: SCAN_POINTS (200) consecutive samples around the centre
// frequency, each { rssi, snr }. Once a scan completes, the data stays
// valid until scanReset / scanAbort. drawScanGraphs() reads samples
// through scanGetRSSI / scanGetSNR, which normalise to 0..1 using the
// observed min / max across the sweep.
// ============================================================================

#include "Scan.h"

#include <Arduino.h>
#include <string.h>

#include "radio.h"

namespace {

constexpr uint16_t SCAN_POINTS    = 200;
constexpr uint8_t  SCAN_OFF       = 0;
constexpr uint8_t  SCAN_RUN       = 1;
constexpr uint8_t  SCAN_DONE      = 2;

// Tuning settle times match upstream's defines.
constexpr uint16_t TUNE_DELAY_FM     = 60;
constexpr uint16_t TUNE_DELAY_AM_SSB = 80;

struct Sample { uint8_t rssi; uint8_t snr; };

Sample  g_data[SCAN_POINTS];
uint8_t g_status = SCAN_OFF;

uint16_t g_startFreq  = 0;
uint16_t g_step       = 0;
uint16_t g_count      = 0;
uint16_t g_savedFreq  = 0;  // freq the radio was on before scanStart()
uint8_t  g_minRssi   = 255;
uint8_t  g_maxRssi   = 0;
uint8_t  g_minSnr    = 255;
uint8_t  g_maxSnr    = 0;

uint16_t g_settleMs = TUNE_DELAY_FM;

inline uint8_t umin(uint8_t a, uint8_t b) { return a < b ? a : b; }
inline uint8_t umax(uint8_t a, uint8_t b) { return a > b ? a : b; }

}  // namespace

void scanStart(uint16_t centerFreq, uint16_t step) {
    if (g_status == SCAN_RUN) return;

    g_savedFreq = centerFreq;   // remember the "home" tune for scanAbort
    g_step      = step ? step : 1;
    g_count     = 0;
    g_minRssi  = 255;
    g_maxRssi  = 0;
    g_minSnr   = 255;
    g_maxSnr   = 0;
    g_status   = SCAN_RUN;

    const Band *band = radioGetCurrentBand();
    g_settleMs       = (band->mode == MODE_FM) ? TUNE_DELAY_FM : TUNE_DELAY_AM_SSB;

    // Align sweep around centre, nudging so the window sits inside the
    // band even when the user tuned very close to an edge.
    int32_t start = (int32_t)g_step * ((int32_t)centerFreq / g_step - (int32_t)SCAN_POINTS / 2);
    if (start + (int32_t)g_step * (SCAN_POINTS - 1) > (int32_t)band->maxFreq)
        start = (int32_t)band->maxFreq - (int32_t)g_step * (SCAN_POINTS - 1);
    if (start < (int32_t)band->minFreq) start = (int32_t)band->minFreq;
    g_startFreq = (uint16_t)start;

    memset(g_data, 0, sizeof(g_data));

    radioScanEnter();
    Serial.printf("[scan] started center=%u step=%u start=%u settle=%ums\n",
                  (unsigned)centerFreq, (unsigned)g_step,
                  (unsigned)g_startFreq, (unsigned)g_settleMs);
}

bool scanTick() {
    if (g_status != SCAN_RUN) return false;
    if (g_count >= SCAN_POINTS) {
        g_status = SCAN_DONE;
        Serial.println(F("[scan] complete"));
        return false;
    }

    uint16_t freq = g_startFreq + g_step * g_count;
    const Band *band = radioGetCurrentBand();
    if (freq < band->minFreq || freq > band->maxFreq) {
        g_status = SCAN_DONE;
        Serial.println(F("[scan] ended (band edge)"));
        return false;
    }

    uint8_t rssi = 0, snr = 0;
    radioScanMeasure(freq, g_settleMs, rssi, snr);

    g_data[g_count].rssi = rssi;
    g_data[g_count].snr  = snr;
    g_minRssi = umin(rssi, g_minRssi);
    g_maxRssi = umax(rssi, g_maxRssi);
    g_minSnr  = umin(snr, g_minSnr);
    g_maxSnr  = umax(snr, g_maxSnr);

    // Progress log every 20 samples — enough to confirm the sweep is
    // actually running without drowning the serial console.
    if ((g_count % 20) == 0) {
        Serial.printf("[scan] %u/%u freq=%u rssi=%u snr=%u\n",
                      (unsigned)g_count, (unsigned)SCAN_POINTS,
                      (unsigned)freq, (unsigned)rssi, (unsigned)snr);
    }

    if (++g_count >= SCAN_POINTS) g_status = SCAN_DONE;
    return g_status == SCAN_RUN;
}

void scanAbort() {
    if (g_status == SCAN_OFF) return;
    g_status = SCAN_OFF;
    radioScanExit(g_savedFreq);
}

bool scanIsActive() {
    return g_status != SCAN_OFF;
}

void scanReset() {
    g_status = SCAN_OFF;
}

// Progressive readout: samples become visible as soon as they are
// collected (idx < g_count), instead of blocking until SCAN_DONE. This
// lets drawScanGraphs paint the bars growing across the zone as the
// sweep rolls left-to-right, confirming visually that the sweep is
// alive even before it finishes.

float scanGetRSSI(uint16_t freq) {
    if (g_status == SCAN_OFF) return 0.0f;
    if (freq < g_startFreq) return 0.0f;
    uint32_t idx = (uint32_t)(freq - g_startFreq) / g_step;
    if (idx >= g_count) return 0.0f;
    if (g_maxRssi <= g_minRssi) return 0.0f;
    uint8_t range = g_maxRssi - g_minRssi + 1;
    return (g_data[idx].rssi - g_minRssi) / (float)range;
}

float scanGetSNR(uint16_t freq) {
    if (g_status == SCAN_OFF) return 0.0f;
    if (freq < g_startFreq) return 0.0f;
    uint32_t idx = (uint32_t)(freq - g_startFreq) / g_step;
    if (idx >= g_count) return 0.0f;
    if (g_maxSnr <= g_minSnr) return 0.0f;
    uint8_t range = g_maxSnr - g_minSnr + 1;
    return (g_data[idx].snr - g_minSnr) / (float)range;
}
