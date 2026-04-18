// ============================================================================
// input.h — rotary encoder wrapper.
//
// Encoder pins (A=18, B=19, BTN=5) are fixed in input.cpp. Touch handling
// is intentionally NOT declared here; it lives in main.cpp so the radio
// / menu layers don't need to pull TFT_eSPI.h for a helper they'd never
// call directly.
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

// Encoder-button events distinguished by encoderPollButton(). The long-press
// threshold (BTN_LONG_PRESS_MS in input.cpp) fires BTN_LONG_PRESS *while the
// button is still held* — not on release — so the UI can react immediately.
// The subsequent release is swallowed silently so a long press never also
// emits a BTN_CLICK. Mirror of ATS-Mini's ButtonTracker click / long-press
// split (their source: ats-mini/Button.h).
enum ButtonEvent : uint8_t {
    BTN_NONE = 0,
    BTN_CLICK,
    BTN_LONG_PRESS
};

// Set up pins + ISR + acceleration. Call once from setup().
void encoderInit();

// Reconfigure encoder boundaries for the given mode. In FREQUENCY mode the
// bounds come from the current band (see radio.h radioGetCurrentBand()); the
// encoder value is stored in `freq / band->step` units so the raw count fits
// in a long. In VOLUME mode it's a plain 0..MAX_VOLUME range.
void encoderSetBoundsForMode(AdjustMode mode, uint16_t freq, uint8_t vol);

// Reconfigure the encoder for menu navigation: wide no-wrap bounds, reset to
// zero. Callers read the raw value and compute deltas; a +/-N tick physical
// rotation becomes a +/-N delta returned by encoderPollRotation(). On menu
// close the previous mode is restored via encoderSetBoundsForMode().
void encoderSetBoundsForMenu();

// Returns true when the encoder has moved since the last poll. On true, fills
// `outValue` with the new raw encoder value (scaled as described above).
bool encoderPollRotation(long& outValue);

// Poll the encoder button. Returns:
//   BTN_NONE       — no edge this poll
//   BTN_CLICK      — button was pressed then released within BTN_LONG_PRESS_MS
//   BTN_LONG_PRESS — button has been held past the long-press threshold (fires
//                    exactly once per hold; subsequent release is silent)
ButtonEvent encoderPollButton();

#endif  // INPUT_H
