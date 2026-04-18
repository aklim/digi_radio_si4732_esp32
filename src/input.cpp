// ============================================================================
// input.cpp — rotary encoder wrapper (pins + ISR + acceleration + state).
//
// All pin numbers, ISR handling, and the AiEsp32RotaryEncoder instance are
// owned by this translation unit. Both main.cpp (OLED) and main_tft.cpp (TFT)
// talk to the encoder through the five functions declared in input.h.
// ============================================================================

#include "input.h"
#include "radio.h"   // FM_FREQ_MIN/MAX/STEP, MAX_VOLUME

#include <Arduino.h>
#include <AiEsp32RotaryEncoder.h>

// --- Pin map (same on both display variants) --------------------------------
constexpr uint8_t ENCODER_PIN_A   = 18;
constexpr uint8_t ENCODER_PIN_B   = 19;
constexpr int     ENCODER_PIN_BTN = 5;
constexpr int     ENCODER_PIN_VCC = -1;  // encoder powered externally

// Some encoders emit 4 state changes per physical detent instead of 2, causing
// the count to jump by 2 per click. The flag below compensates for that; flip
// to false if you fit an encoder that already reports 2 states per detent.
constexpr bool    ENCODER_HALF_STEP_CORRECTION = true;
constexpr uint8_t ENCODER_STEPS = ENCODER_HALF_STEP_CORRECTION ? 4 : 2;

// --- Owned state ------------------------------------------------------------
static AiEsp32RotaryEncoder encoder(ENCODER_PIN_A, ENCODER_PIN_B,
                                     ENCODER_PIN_BTN, ENCODER_PIN_VCC,
                                     ENCODER_STEPS);

// Free function in IRAM — AiEsp32RotaryEncoder requires a non-member ISR.
static void IRAM_ATTR readEncoderISR() {
    encoder.readEncoder_ISR();
}

// ============================================================================
// Public API
// ============================================================================

void encoderInit() {
    encoder.begin();
    encoder.setup(readEncoderISR);
    encoder.setAcceleration(100);
}

void encoderSetBoundsForMode(AdjustMode mode, uint16_t freq, uint8_t vol) {
    if (mode == MODE_FREQUENCY) {
        // Encoder values represent freq/step so the raw reading fits in a long
        // (870..1080 for FM). Wrap-around at band edges matches OLED v1.0.0.
        encoder.setBoundaries(FM_FREQ_MIN / FM_FREQ_STEP,
                              FM_FREQ_MAX / FM_FREQ_STEP, true);
        encoder.reset(freq / FM_FREQ_STEP);
        encoder.setAcceleration(100);
    } else {
        encoder.setBoundaries(0, MAX_VOLUME, false);  // no wrap on volume
        encoder.reset(vol);
        encoder.setAcceleration(50);
    }
}

bool encoderPollRotation(long& outValue) {
    if (encoder.encoderChanged() == 0) return false;
    outValue = encoder.readEncoder();
    return true;
}

bool encoderPollButton() {
    return encoder.isEncoderButtonClicked();
}
