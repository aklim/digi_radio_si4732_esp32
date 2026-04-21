// ============================================================================
// backlight.cpp — LEDC PWM driver for the TFT backlight pin.
//
// Pattern matches the bring-up fixture in test_shield.cpp: channel 0, 5 kHz
// carrier, 8-bit resolution. The percent-based public API hides the 0..255
// duty from callers (menu, persist) so the storage format can be changed
// without touching them.
// ============================================================================

#include "backlight.h"

#include <Arduino.h>
#include <User_Setup.h>   // TFT_BL pin macro

namespace {

constexpr uint8_t  LEDC_CHANNEL   = 0;
constexpr uint32_t LEDC_FREQ_HZ   = 5000;
constexpr uint8_t  LEDC_RES_BITS  = 8;

uint8_t g_percent = BACKLIGHT_DEFAULT_PERCENT;

inline uint8_t percentToDuty(uint8_t p) {
    if (p > 100) p = 100;
    // Round half-up so 50% → 128 (not 127); matches user expectations for
    // a "halfway" setting and avoids the off-by-one at BACKLIGHT_LEVELS[]
    // edges (20% → 51, 60% → 153, 100% → 255).
    // The explicit uint16_t promotion before the cast is load-bearing:
    // without it, `(uint8_t)(p * 255)` truncates first (e.g. 100*255=25500
    // wraps to 156 in uint8), then `/100` gives a bogus duty of 0..2.
    uint16_t v = ((uint16_t)p * 255u + 50u) / 100u;
    return (uint8_t)v;
}

}  // namespace

void backlightInit() {
    ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcAttachPin(TFT_BL, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, percentToDuty(g_percent));
}

void backlightApply(uint8_t percent) {
    if (percent > 100) percent = 100;
    g_percent = percent;
    ledcWrite(LEDC_CHANNEL, percentToDuty(percent));
}

uint8_t backlightGet() {
    return g_percent;
}
