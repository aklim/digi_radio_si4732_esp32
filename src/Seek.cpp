// ============================================================================
// Seek.cpp — auto-seek state machine.
//
// Mirrors Scan.cpp (the bandscope sweep) but with different stop
// conditions: seek halts as soon as a step yields RSSI >= RSSI_THRESH and
// SNR >= SNR_THRESH for the current mode. On miss it keeps stepping;
// wraps at band edges; bails if it walks the full band back to the
// origin without a hit.
//
// Why not the SI4735 library's seekStationUp/Down? Hardware seek works
// well on FM but is flaky on AM/SW, and exposes no threshold control. A
// software step-loop through radioScanMeasure() gives us a single code
// path for every band and lets the user's perception of "listenable"
// match the threshold we pick here.
// ============================================================================

#include "Seek.h"

#include <Arduino.h>

#include "radio.h"
#include "seek_step.h"

namespace {

constexpr uint8_t SEEK_OFF = 0;
constexpr uint8_t SEEK_RUN = 1;

// Settle time per measurement. Matches Scan.cpp's bandscope settle — the
// SI4735 takes ~50-60 ms after a retune to produce a stable RSSI/SNR
// reading on FM, and going lower makes the first-hit RSSI noisy enough
// to trip on adjacent-channel bleed. An unpopulated FM band covers
// ~210 steps at 60 ms = ~13 s end-to-end, which is still acceptable
// for a user-initiated seek.
constexpr uint16_t SETTLE_MS_FM     = 60;
constexpr uint16_t SETTLE_MS_AM_SSB = 80;

// Quality thresholds. FM numbers are calibrated against Layout-Default's
// `strengthFromRssi` (S8 ≈ RSSI 14, the weakest "listenable" reading).
// AM/SW use the denser S-point table and a noisier noise floor, so the
// bar is higher. Both modes demand a minimum SNR to reject bursty AGC
// amplification of pure noise.
constexpr uint8_t RSSI_THRESH_FM = 15;
constexpr uint8_t SNR_THRESH_FM  = 8;
constexpr uint8_t RSSI_THRESH_AM = 25;
constexpr uint8_t SNR_THRESH_AM  = 8;

uint8_t  g_state      = SEEK_OFF;
SeekDir  g_dir        = SEEK_UP;
uint16_t g_originFreq = 0;   // for "no hit, restore" and wrap-detection
uint16_t g_cursor     = 0;   // freq of the next seekTick measurement
uint16_t g_maxSteps   = 0;   // upper bound on ticks before giving up

inline bool meetsThresholds(uint8_t rssi, uint8_t snr) {
    const Band *b = radioGetCurrentBand();
    if (b && b->mode == MODE_FM) {
        return rssi >= RSSI_THRESH_FM && snr >= SNR_THRESH_FM;
    }
    return rssi >= RSSI_THRESH_AM && snr >= SNR_THRESH_AM;
}

}  // namespace

void seekStart(SeekDir dir) {
    if (g_state == SEEK_RUN) return;

    const Band *band = radioGetCurrentBand();
    if (!band || band->step == 0) return;

    g_dir        = dir;
    g_originFreq = radioGetFrequency();
    g_cursor     = seekNextFreq(g_originFreq, band->minFreq, band->maxFreq,
                                band->step, dir);

    // Cap the run at one full band traversal so a dead band can't loop
    // forever if floating-point edge cases ever make the origin-equality
    // wrap-detector miss. +2 for the wrap seam.
    uint32_t span  = (uint32_t)(band->maxFreq - band->minFreq);
    g_maxSteps     = (uint16_t)(span / band->step) + 2;
    g_state        = SEEK_RUN;

    radioScanEnter();
    Serial.printf("[seek] start dir=%s origin=%u first=%u max=%u\n",
                  dir == SEEK_UP ? "up" : "down",
                  (unsigned)g_originFreq, (unsigned)g_cursor,
                  (unsigned)g_maxSteps);
}

bool seekTick() {
    if (g_state != SEEK_RUN) return false;

    const Band *band = radioGetCurrentBand();
    if (!band) { seekAbort(); return false; }

    uint16_t settle = (band->mode == MODE_FM) ? SETTLE_MS_FM : SETTLE_MS_AM_SSB;
    uint8_t  rssi = 0, snr = 0;
    radioScanMeasure(g_cursor, settle, rssi, snr);

    if (meetsThresholds(rssi, snr)) {
        // The first frequency whose RSSI clears the threshold is usually
        // one channel off the true carrier — the SI4735's IF filter leaks
        // ~20 dBuV into the adjacent 100 kHz slot on FM, and the
        // carrier's own skirt on AM/SW is similarly wide. Keep probing in
        // the seek direction while RSSI keeps rising so we land on the
        // actual peak, not the skirt. Capped at 3 extra measurements so
        // a very strong station with a wide skirt can't wander the loop
        // across multiple channels.
        uint16_t peakCursor = g_cursor;
        uint8_t  peakRssi   = rssi;
        for (int i = 0; i < 3; i++) {
            uint16_t probe = seekNextFreq(peakCursor, band->minFreq, band->maxFreq,
                                          band->step, g_dir);
            if (probe == g_originFreq) break;  // don't cross origin
            uint8_t pRssi = 0, pSnr = 0;
            radioScanMeasure(probe, settle, pRssi, pSnr);
            if (pRssi > peakRssi) {
                peakCursor = probe;
                peakRssi   = pRssi;
            } else {
                break;
            }
        }
        Serial.printf("[seek] hit freq=%u rssi=%u -> peak %u rssi=%u\n",
                      (unsigned)g_cursor, (unsigned)rssi,
                      (unsigned)peakCursor, (unsigned)peakRssi);
        // Land on the peak. radioScanExit() retunes the chip, updates the
        // cached band freq, and un-mutes (respecting the user mute latch).
        g_state = SEEK_OFF;
        radioScanExit(peakCursor);
        return false;
    }

    if (g_maxSteps == 0 || g_cursor == g_originFreq) {
        Serial.printf("[seek] no hit, restoring origin=%u\n",
                      (unsigned)g_originFreq);
        g_state = SEEK_OFF;
        radioScanExit(g_originFreq);
        return false;
    }

    g_cursor = seekNextFreq(g_cursor, band->minFreq, band->maxFreq,
                            band->step, g_dir);
    g_maxSteps--;
    return true;
}

void seekAbort() {
    if (g_state == SEEK_OFF) return;
    Serial.printf("[seek] abort, restoring origin=%u\n",
                  (unsigned)g_originFreq);
    g_state = SEEK_OFF;
    radioScanExit(g_originFreq);
}

bool seekIsActive() { return g_state != SEEK_OFF; }

SeekDir seekDirection() { return g_dir; }
