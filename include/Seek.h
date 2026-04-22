// ============================================================================
// Seek.h — auto-seek state machine.
//
// Non-blocking seek to the next frequency whose RSSI and SNR exceed
// per-mode thresholds. Ticked from the main loop the same way Scan.cpp is;
// shares radio.cpp's scanEnter/Measure/Exit hooks so the Core-0 poll loop
// stays out of the way. See Scan.cpp for the template this mirrors.
//
// UI contract: while seekIsActive() the main input router treats any
// button or rotation as an abort (same semantics as Scan). Button row
// in Layout-Default.cpp draws the active direction's button highlighted
// via seekDirection().
// ============================================================================

#ifndef SEEK_H
#define SEEK_H

#include <stdint.h>

#include "seek_step.h"   // SeekDir enum

// Start a seek in `dir` from the current tuned frequency. No-op if a seek
// is already running. Mutes audio via radioScanEnter() for the duration
// so the user doesn't hear the squelch across each step.
void seekStart(SeekDir dir);

// Advance one step. Must be called from the Arduino loop each iteration
// while active. Returns true while still seeking, false when the seek
// stopped (hit a station or exhausted the band).
bool seekTick();

// Cancel a seek in progress, restoring the frequency from before
// seekStart(). Safe to call when not active — no-op.
void seekAbort();

// True while seekStart has been called and seek has neither landed on a
// station nor exhausted the band.
bool seekIsActive();

// Direction of the currently-running seek. Undefined when not active.
SeekDir seekDirection();

#endif  // SEEK_H
