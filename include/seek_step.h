// ============================================================================
// seek_step.h — pure frequency-step helper for the auto-seek state machine.
//
// Extracted from Seek.cpp so the wrap-around arithmetic can be unit-tested
// without pulling Arduino / SI4735 / FreeRTOS. The tests live in
// test/test_native_seek/.
//
// Seek steps the tune one `step` at a time in `dir`. When it walks past a
// band edge it wraps to the opposite edge (continues the search). When the
// cursor lands back on the origin frequency after wrapping, the caller
// knows the band has been fully scanned and no station met the threshold.
// ============================================================================

#ifndef SEEK_STEP_H
#define SEEK_STEP_H

#include <stdint.h>

enum SeekDir : int8_t {
    SEEK_UP   = +1,
    SEEK_DOWN = -1,
};

// Step `current` by `step` in `dir` within [minF, maxF]. Wraps at the band
// edge: stepping past maxF lands on minF, stepping below minF lands on maxF.
// Inline + pure so the native test env can link it directly.
//
// Preconditions: step > 0, minF < maxF, current in [minF, maxF]. Callers
// within this firmware always satisfy these — the Band table guarantees
// min/max ordering and positive step.
inline uint16_t seekNextFreq(uint16_t current,
                             uint16_t minF,
                             uint16_t maxF,
                             uint16_t step,
                             SeekDir  dir) {
    if (dir == SEEK_UP) {
        uint32_t next = (uint32_t)current + step;
        if (next > maxF) return minF;
        return (uint16_t)next;
    } else {
        if (current < minF + step) return maxF;
        return (uint16_t)(current - step);
    }
}

#endif  // SEEK_STEP_H
