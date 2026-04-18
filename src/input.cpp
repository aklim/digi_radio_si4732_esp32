// ============================================================================
// input.cpp — rotary encoder wrapper (pins + ISR + acceleration + state).
//
// All pin numbers, ISR handling, and the AiEsp32RotaryEncoder instance are
// owned by this translation unit. main.cpp + menu.cpp talk to the encoder
// through the functions declared in input.h.
//
// Long-press handling lives here too: the underlying library only exposes a
// click-on-release signal, so encoderPollButton() runs its own small state
// machine on top of `isEncoderButtonDown()` to distinguish BTN_CLICK from
// BTN_LONG_PRESS. Threshold matches ATS-Mini's click-vs-long-press split.
// ============================================================================

#include "input.h"
#include "radio.h"   // radioGetCurrentBand(), MAX_VOLUME

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

// Long-press threshold. 500 ms matches ATS-Mini's SHORT_PRESS_INTERVAL — feels
// snappy on a 2-deep menu without being hair-trigger. Re-tune in hardware
// testing if it becomes annoying (raising to 700 ms is the usual fix).
constexpr unsigned long BTN_LONG_PRESS_MS = 500;

// --- Owned state ------------------------------------------------------------
static AiEsp32RotaryEncoder encoder(ENCODER_PIN_A, ENCODER_PIN_B,
                                     ENCODER_PIN_BTN, ENCODER_PIN_VCC,
                                     ENCODER_STEPS);

// Button-press tracker state. Kept here instead of inside a class because the
// only caller is encoderPollButton() and making it static-local would defeat
// the "exactly one instance" contract (unit tests, if ever added, would want
// a reset entry point — encoderInit() clears it).
static bool          g_btnPressActive = false;
static bool          g_btnLongFired   = false;
static unsigned long g_btnPressStart  = 0;

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

    g_btnPressActive = false;
    g_btnLongFired   = false;
    g_btnPressStart  = 0;
}

void encoderSetBoundsForMenu() {
    // Huge range with no wrap — the menu layer compares the raw value
    // against its own previous read to compute a delta, and handles cursor
    // wrapping on its side. Acceleration is off so single detents don't get
    // amplified into 3-5 item skips inside the tiny menu lists.
    encoder.setBoundaries(-10000, 10000, false);
    encoder.reset(0);
    encoder.setAcceleration(0);
}

void encoderSetBoundsForMode(AdjustMode mode, uint16_t freq, uint8_t vol) {
    if (mode == MODE_FREQUENCY) {
        // Encoder stores freq/step so the raw long stays bounded even at the
        // widest SW range. The band table is the source of truth for min/max
        // and step — same helper handles FM (10 kHz units) and AM (1 kHz
        // units) because the radio layer expresses both in its own native
        // unit; setBoundaries just wraps whatever we pass.
        const Band* b = radioGetCurrentBand();
        encoder.setBoundaries(b->minFreq / b->step, b->maxFreq / b->step, true);
        encoder.reset(freq / b->step);
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

ButtonEvent encoderPollButton() {
    bool          down = encoder.isEncoderButtonDown();
    unsigned long now  = millis();

    if (down) {
        if (!g_btnPressActive) {
            // Rising edge — start the long-press timer. Don't emit anything
            // yet; we don't know if this will turn into a click or a long.
            g_btnPressActive = true;
            g_btnPressStart  = now;
            g_btnLongFired   = false;
            return BTN_NONE;
        }
        // Button is still held. Fire BTN_LONG_PRESS once when we cross the
        // threshold; subsequent polls during the same hold return BTN_NONE.
        if (!g_btnLongFired && (now - g_btnPressStart >= BTN_LONG_PRESS_MS)) {
            g_btnLongFired = true;
            return BTN_LONG_PRESS;
        }
        return BTN_NONE;
    }

    // Button is up.
    if (g_btnPressActive) {
        g_btnPressActive = false;
        // Release after a long press is silent — the UI already handled the
        // press via BTN_LONG_PRESS earlier. A short press emits BTN_CLICK.
        if (!g_btnLongFired) {
            return BTN_CLICK;
        }
        g_btnLongFired = false;
    }
    return BTN_NONE;
}
