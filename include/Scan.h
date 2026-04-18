// ============================================================================
// Scan.h — bandscope (RSSI / SNR sweep) driver.
//
// Modelled on ats-mini/Scan.cpp. Upstream runs the sweep synchronously from
// its main loop; we keep the tick-driven API so our main loop can call
// scanTick() every iteration without blocking for the full multi-second
// sweep. Scan owns the radio while active — the Core-0 polling task is
// silenced via radioScanEnter() / radioScanExit() in radio.cpp.
// ============================================================================

#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>

// Begin a sweep centred on `centerFreq` (native band units — 10 kHz on FM,
// 1 kHz on AM / MW / SW). Upstream uses a fixed SCAN_POINTS window (200
// samples); we inherit that constant. Clamps the start freq to the
// active band edges. Calling scanStart while already running is a no-op.
void scanStart(uint16_t centerFreq, uint16_t step);

// Advance the sweep by one sample. Returns true while the sweep is still
// in progress, false when it has completed (data ready) or wasn't started.
// Safe to call every main-loop iteration; internally rate-limited to the
// chip's tuning settle time.
bool scanTick();

// Cancel an in-progress (or completed-but-held) sweep and re-tune the
// radio to the frequency it was on before scanStart() ran.
void scanAbort();

// True while a sweep is either running or completed-with-data-held.
// Layout-Default consults this to switch the bottom zone from the
// static band scale to the sweep graph.
bool scanIsActive();

// Release the captured sweep back to the scan-off state. Call this when
// leaving scan mode — unlike scanAbort this does NOT retune the radio
// (caller already handled the tune, e.g. via radioScanExit).
void scanReset();

// Normalised sweep values for the given frequency (same native units).
// Returns 0.0..1.0; 0.0 when scan data is not available for `freq`.
// Consumed by drawScanGraphs().
float scanGetRSSI(uint16_t freq);
float scanGetSNR(uint16_t freq);

#endif  // SCAN_H
