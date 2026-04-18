// ============================================================================
// input.h — rotary encoder wrapper shared by the OLED and TFT firmwares.
//
// Encoder pins (A=18, B=19, BTN=5) are fixed in input.cpp and will be reused
// on both display variants — only the display itself differs between builds.
//
// Touch handling is intentionally NOT declared here; it lives in main_tft.cpp
// so the OLED firmware does not need to pull in TFT_eSPI just to get a touch
// helper that it would never call.
// ============================================================================

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

// Adjustment mode — what the encoder rotation currently controls. Both mains
// hold a `currentMode` of this type; the encoder bounds/wrap-around behavior
// is reconfigured via encoderSetBoundsForMode() when it changes.
enum AdjustMode {
    MODE_FREQUENCY,
    MODE_VOLUME
};

// Set up pins + ISR + acceleration. Call once from setup().
void encoderInit();

// Reconfigure encoder boundaries for the given mode. `freq` is in 10 kHz
// units (matches the Si4735 library); `vol` is in 0..MAX_VOLUME. The encoder
// is reset to the supplied current value so a subsequent poll returns it
// unchanged.
void encoderSetBoundsForMode(AdjustMode mode, uint16_t freq, uint8_t vol);

// Returns true when the encoder has moved since the last poll. On true, fills
// `outValue` with the new raw encoder value (frequency/FM_FREQ_STEP units in
// FREQUENCY mode, 0..MAX_VOLUME in VOLUME mode).
bool encoderPollRotation(long& outValue);

// Returns true when the encoder button has been clicked (debounced by the
// underlying library, up to ~300 ms latency).
bool encoderPollButton();

#endif  // INPUT_H
