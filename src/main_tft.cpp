// ============================================================================
// Digital Radio Receiver Firmware — TFT variant (env:esp32dev_tft)
// Hardware: ESP32 + Si4732 + Waveshare 2.8" TFT Touch Shield Rev 2.1
//           (ST7789V 240x320 + XPT2046 resistive touch) + Rotary Encoder
//
// Radio and encoder logic live in radio.cpp / input.cpp (shared with the OLED
// variant at src/main.cpp). Only the TFT-specific UI, backlight PWM, and
// Arduino setup/loop entry points live here.
//
// Pin split (zero conflicts between Si4732 I2C and TFT HSPI — see
// docs/hardware.md):
//   Si4732 + I2C : SDA=21, SCL=22
//   Encoder      : A=18, B=19, BTN=5
//   TFT HSPI     : MOSI=13, SCLK=14, MISO=27, CS=15, DC=2, RST=33
//   Backlight    : GPIO 4 (LEDC PWM)
//   Touch CS     : GPIO 17 (XPT2046 — calibration baked in below)
//
// Rendering: partial redraw driven by a per-zone dirty-flags byte. Each zone
// is repainted independently by clearing its rect to COL_BG and redrawing
// content with drawString() + fillRect()/drawRect(). Benchmark in
// docs/display_shield_test.md (1.83 us/drawString, 46 ms polled full-fill)
// shows this is fast enough for the <=10 Hz UI rate we trigger in practice.
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
// LOAD_GFXFF in User_Setup.h causes TFT_eSPI.h to pull in
// Fonts/GFXFF/gfxfont.h, which in turn includes every FreeXxxNpt7b font
// header. Unused fonts are dropped by --gc-sections at link time, so
// we just reference the struct addresses directly below.

#include "radio.h"
#include "input.h"
#include "ui_layout_tft.h"

#include "version.h"

// Adafruit-GFX free fonts used across the UI. Aliased here so every draw
// function refers to a single source of truth and a theme / typography
// tweak is a one-line change. Each alias is the address of the font's
// GFXfont struct (the same value that Free_Fonts.h macros like FSSB12
// expand to in the TFT_eSPI examples).
//   FREQ_FONT       — frequency digits + MHz label (large, bold, sans)
//   HEADER_FONT     — "FM", "STEREO"/"MONO", "Vol"
//   LABEL_FONT      — section labels (RSSI, SNR, PS:, footer text)
//   VALUE_FONT      — numeric values next to the labels
static const GFXfont* const FREQ_FONT   = &FreeSansBold24pt7b;  // ~34 px cap height
static const GFXfont* const HEADER_FONT = &FreeSansBold12pt7b;  // ~17 px cap height
static const GFXfont* const LABEL_FONT  = &FreeSans9pt7b;       // ~13 px cap height
static const GFXfont* const VALUE_FONT  = &FreeSansBold9pt7b;   // ~13 px cap height, bold

// ============================================================================
// Section 1: Pins & constants
// ============================================================================

constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// Backlight PWM (LEDC — matches the pattern from test_shield.cpp).
constexpr uint8_t  BL_LEDC_CHANNEL  = 0;
constexpr uint32_t BL_LEDC_FREQ_HZ  = 5000;
constexpr uint8_t  BL_LEDC_RES_BITS = 8;
constexpr uint8_t  BL_DEFAULT_DUTY  = 220;

// Hard-coded XPT2046 calibration — produced once by tft.calibrateTouch() on
// this specific shield; see src/test_shield.cpp where the same constants
// have been running reliably since the bring-up. If a replacement shield
// lands, re-run the calibration phase from test_shield.cpp and paste the
// new 5 values here.
static uint16_t TOUCH_CALIBRATION[5] = { 477, 3203, 487, 3356, 6 };

// Firmware identity — same pattern as main.cpp; the `asm volatile` anchor in
// setup() keeps the symbol alive against --gc-sections. Unlike the OLED,
// the TFT variant renders FW_VERSION on-screen (footer + header), since the
// 240x320 canvas has the room and the user asked for it.
extern "C" __attribute__((used)) const char FW_IDENTITY[] =
    "FW=" FW_VERSION " commit=" FW_GIT_COMMIT " built=" FW_BUILD_DATE;

// ============================================================================
// Section 2: Dirty-flags pipeline
// ============================================================================

constexpr uint8_t DIRTY_HEADER = 1 << 0;
constexpr uint8_t DIRTY_FREQ   = 1 << 1;
constexpr uint8_t DIRTY_RDS    = 1 << 2;
constexpr uint8_t DIRTY_METER  = 1 << 3;
constexpr uint8_t DIRTY_VOL    = 1 << 4;
constexpr uint8_t DIRTY_FOOTER = 1 << 5;
constexpr uint8_t DIRTY_ALL    = 0x3F;

static uint8_t dirtyFlags = DIRTY_ALL;

static inline void markDirty(uint8_t bits) { dirtyFlags |= bits; }

// ============================================================================
// Section 3: Globals & state
// ============================================================================

static TFT_eSPI tft = TFT_eSPI();

// Sprite used by the analog needle S-meter. Allocated once in setup() at
// GAUGE_W x GAUGE_H pixels (2 bytes each -> ~14 KB of heap) and re-used
// every animation frame. Drawing into a sprite and pushing the whole rect
// gives flicker-free redraws, which direct tft.drawLine() can't: the old
// needle has to be erased before the new one is drawn, and an xor / wipe
// pass always flashes.
static TFT_eSprite gauge = TFT_eSprite(&tft);

// Smoothed RSSI for the needle. Updated every NEEDLE_ANIM_MS from loop()
// via a simple exponential moving average toward radioGetRssi(), giving
// the needle a critically-damped mechanical feel. Starts at 0 so the
// needle sweeps up from rest at boot.
static float g_rssiDisplay = 0.0f;
// Last drawn needle value (in dBuV-ish units, same scale as g_rssiDisplay).
// Used to gate redraws — if the smoothed value hasn't moved more than
// NEEDLE_EPSILON since the last paint, skip the frame.
static float g_rssiLastDrawn = -999.0f;

constexpr unsigned long NEEDLE_ANIM_MS = 30;   // redraw cadence (~33 Hz)
constexpr float NEEDLE_EMA_ALPHA = 0.25f;      // ~400 ms to new steady-state
constexpr float NEEDLE_EPSILON   = 0.2f;       // redraw threshold (dBuV)

static AdjustMode currentMode = MODE_FREQUENCY;

// ============================================================================
// Section 4: Forward declarations
// ============================================================================

static void initBacklight();
static void initDisplay();
static void initGauge();
static void drawSplash();

static void updateDisplay();
static void drawHeader();
static void drawFrequency();
static void drawRds();
static void drawMeter();
static void drawVolume();
static void drawFooter();
static void drawNeedleGauge(float dbuv);
static void pumpNeedleAnimation();

static void handleEncoderRotation(long value);
static void toggleMode();
static void setMode(AdjustMode newMode);
static void handleTouch();

// ============================================================================
// Section 5: setup() / loop()
// ============================================================================

void setup() {
    asm volatile("" : : "r"(FW_IDENTITY));

    Serial.begin(115200);
    Serial.println(F("Digital Radio (TFT) — starting up..."));

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    initBacklight();
    initDisplay();
    initGauge();
    tft.setTouch(TOUCH_CALIBRATION);
    drawSplash();

    // Wait for the Si4732 RC reset circuit to release the chip.
    delay(500);

    radioInit();
    encoderInit();
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());

    tft.fillScreen(COL_BG);
    dirtyFlags = DIRTY_ALL;
    Serial.println(F("Digital Radio (TFT) — ready."));
}

void loop() {
    long encValue;
    if (encoderPollRotation(encValue)) {
        handleEncoderRotation(encValue);
    }
    if (encoderPollButton()) {
        toggleMode();
    }
    if (radioPollSignal()) {
        // Stereo pilot lives in the header; RSSI/SNR in the meter zone.
        markDirty(DIRTY_METER | DIRTY_HEADER);
    }
    if (radioPollRds()) {
        markDirty(DIRTY_RDS);
    }
    handleTouch();
    pumpNeedleAnimation();
    updateDisplay();
}

// ============================================================================
// Section 6: Input handling
// ============================================================================

static void handleEncoderRotation(long value) {
    if (currentMode == MODE_FREQUENCY) {
        uint16_t newFreq = (uint16_t)(value * FM_FREQ_STEP);
        if (newFreq < FM_FREQ_MIN) newFreq = FM_FREQ_MIN;
        if (newFreq > FM_FREQ_MAX) newFreq = FM_FREQ_MAX;
        if (newFreq != radioGetFrequency()) {
            radioSetFrequency(newFreq);
            // Tune clears RDS mirrors; repaint the RDS zone so old text goes
            // away immediately instead of waiting for the next 200 ms poll.
            markDirty(DIRTY_FREQ | DIRTY_FOOTER | DIRTY_RDS);
        }
    } else {
        uint8_t newVol = (uint8_t)value;
        if (newVol > MAX_VOLUME) newVol = MAX_VOLUME;
        if (newVol != radioGetVolume()) {
            radioSetVolume(newVol);
            markDirty(DIRTY_VOL);
        }
    }
}

static void setMode(AdjustMode newMode) {
    if (newMode == currentMode) return;
    currentMode = newMode;
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());
    // Focus border colour depends on currentMode — repaint both bordered zones.
    markDirty(DIRTY_FREQ | DIRTY_VOL);
    Serial.print(F("Mode: "));
    Serial.println((currentMode == MODE_FREQUENCY) ? F("FREQUENCY") : F("VOLUME"));
}

static void toggleMode() {
    setMode(currentMode == MODE_FREQUENCY ? MODE_VOLUME : MODE_FREQUENCY);
}

// Poll the XPT2046 touch controller and route taps to the matching mode.
// The library debounces by returning false while the touch is steady; we
// add a 200 ms lockout anyway to absorb panel noise on the Rev 2.1 resistor
// ladder. delay(15) after a processed hit mirrors the pattern that has
// been running in test_shield.cpp since the bring-up.
static void handleTouch() {
    static unsigned long lastTouchMs = 0;
    unsigned long now = millis();
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;

    uint16_t tx = 0, ty = 0;
    if (!tft.getTouch(&tx, &ty)) return;

    // TFT_eSPI's getTouch applies the stored calibration but does not rotate
    // the result. Our calibration was captured in rotation 0, so when the
    // panel is flipped (rotation 2) the raw touch landing at visual (x, y)
    // comes back as (W-1-x, H-1-y) — mirror it here to match the drawn UI.
    if (DISPLAY_FLIPPED) {
        tx = SCREEN_W - 1 - tx;
        ty = SCREEN_H - 1 - ty;
    }

    lastTouchMs = now;

    if (tx >= TOUCH_FREQ_X && tx < TOUCH_FREQ_X + TOUCH_FREQ_W &&
        ty >= TOUCH_FREQ_Y && ty < TOUCH_FREQ_Y + TOUCH_FREQ_H) {
        setMode(MODE_FREQUENCY);
    } else if (tx >= TOUCH_VOL_X && tx < TOUCH_VOL_X + TOUCH_VOL_W &&
               ty >= TOUCH_VOL_Y && ty < TOUCH_VOL_Y + TOUCH_VOL_H) {
        setMode(MODE_VOLUME);
    }

    delay(15);  // let the resistive panel settle
}

// ============================================================================
// Section 7: Display
// ============================================================================

static void initBacklight() {
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_RES_BITS);
    ledcAttachPin(TFT_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, BL_DEFAULT_DUTY);
}

static void initDisplay() {
    tft.init();
    // Rotation 0 = portrait right-side-up; rotation 2 = portrait upside-down
    // for shields mounted in a case that can't be physically flipped. See
    // DISPLAY_FLIPPED in ui_layout_tft.h — flip the flag to switch.
    tft.setRotation(DISPLAY_FLIPPED ? 2 : 0);
    tft.fillScreen(COL_BG);
}

static void drawSplash() {
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_BG);
    tft.setFreeFont(HEADER_FONT);
    tft.drawString("Digital Radio", SCREEN_W / 2, 80);
    tft.setTextColor(COL_VERSION, COL_BG);
    tft.setFreeFont(LABEL_FONT);
    tft.drawString(FW_VERSION, SCREEN_W / 2, 120);
    tft.setTextColor(TFT_DARKGREY, COL_BG);
    tft.drawString("Initializing...", SCREEN_W / 2, 160);
    tft.setTextDatum(TL_DATUM);
}

static void updateDisplay() {
    if (!dirtyFlags) return;

    if (dirtyFlags & DIRTY_HEADER) drawHeader();
    if (dirtyFlags & DIRTY_FREQ)   drawFrequency();
    if (dirtyFlags & DIRTY_RDS)    drawRds();
    if (dirtyFlags & DIRTY_METER)  drawMeter();
    if (dirtyFlags & DIRTY_VOL)    drawVolume();
    if (dirtyFlags & DIRTY_FOOTER) drawFooter();

    dirtyFlags = 0;
}

static void drawHeader() {
    tft.fillRect(0, HEADER_Y, SCREEN_W, HEADER_H, COL_HEADER_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_HEADER_TXT, COL_HEADER_BG);
    tft.setFreeFont(HEADER_FONT);
    tft.drawString("FM", HEADER_MODE_X, HEADER_MODE_Y);

    // Stereo pilot indicator — only meaningful once the radio has locked; we
    // still paint "MONO" when the flag is false so the label position is
    // predictable from boot.
    bool stereo = radioIsStereo();
    tft.setTextColor(stereo ? COL_STEREO_ON : COL_STEREO_OFF, COL_HEADER_BG);
    tft.setFreeFont(LABEL_FONT);
    tft.drawString(stereo ? "STEREO" : " MONO ", HEADER_ST_X, HEADER_ST_Y);

    // Right-aligned version string.
    tft.setTextColor(COL_VERSION, COL_HEADER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(FW_VERSION, HEADER_VER_R, HEADER_VER_Y);
    tft.setTextDatum(TL_DATUM);
}

static void drawFrequency() {
    tft.fillRect(0, FREQ_Y, SCREEN_W, FREQ_H, COL_BG);

    // 1-pixel focus border — colour depends on which mode the encoder drives.
    uint16_t border = (currentMode == MODE_FREQUENCY) ? COL_FOCUS : COL_NOFOCUS;
    tft.drawRect(0, FREQ_Y, SCREEN_W, FREQ_H, border);

    tft.setTextColor(COL_FREQ_TXT, COL_BG);

    uint16_t freq = radioGetFrequency();
    char buf[8];
    snprintf(buf, sizeof(buf), "%u.%u", freq / 100, (freq % 100) / 10);

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FREQ_FONT);
    // Slight left-shift of centre so "MHz" has room on the right.
    tft.drawString(buf, SCREEN_W / 2 - 20, FREQ_Y + FREQ_H / 2);
    tft.setTextDatum(TL_DATUM);

    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.setFreeFont(HEADER_FONT);
    tft.drawString("MHz", FREQ_UNIT_X, FREQ_UNIT_Y);
}

static void drawRds() {
    tft.fillRect(0, RDS_Y, SCREEN_W, RDS_H, COL_BG);

    const char* ps = radioGetRdsPs();
    const char* rt = radioGetRdsRt();

    tft.setTextDatum(TL_DATUM);

    // PS name — "PS:" label in dim grey, then the 8-char name in white. When
    // there is no RDS sync we show "--" so the zone is never empty on boot.
    tft.setFreeFont(HEADER_FONT);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    tft.drawString("PS:", RDS_PS_X, RDS_PS_Y);
    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.drawString(ps && ps[0] ? ps : "--", RDS_PS_X + 54, RDS_PS_Y);

    // RadioText body — regular 9 pt free font, truncated to fit the screen
    // width. Marquee scrolling is v2 (see docs/future_improvements.md).
    tft.setFreeFont(LABEL_FONT);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    if (rt && rt[0]) {
        char line[RDS_RT_MAX_CHARS + 1];
        strncpy(line, rt, RDS_RT_MAX_CHARS);
        line[RDS_RT_MAX_CHARS] = 0;
        tft.drawString(line, RDS_RT_X, RDS_RT_Y);
    }
}

static void drawMeter() {
    tft.fillRect(0, METER_Y, SCREEN_W, METER_H, COL_BG);

    // "RSSI" label (left) + analog needle gauge (center, sprite-backed) +
    // numeric dBuV (right). SNR and the stereo dot live on the second row.
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(LABEL_FONT);
    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.drawString("RSSI", METER_LABEL_X, METER_BAR_Y);

    uint8_t rssi = radioGetRssi();
    char buf[16];
    snprintf(buf, sizeof(buf), "%u dB", rssi);
    tft.setFreeFont(VALUE_FONT);
    tft.drawString(buf, METER_VAL_X, METER_VAL_Y);

    // SNR on the second row, plus a small stereo dot on the far right.
    tft.setFreeFont(LABEL_FONT);
    snprintf(buf, sizeof(buf), "SNR  %u dB", radioGetSnr());
    tft.drawString(buf, METER_LABEL_X, METER_ROW2_Y);

    bool stereo = radioIsStereo();
    tft.fillCircle(STEREO_DOT_X, STEREO_DOT_Y, STEREO_DOT_R,
                   stereo ? COL_STEREO_ON : COL_STEREO_OFF);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    tft.drawString("stereo", STEREO_DOT_X - 58, METER_ROW2_Y);

    // Push a fresh needle gauge frame using the currently-smoothed RSSI so
    // the dial is always in sync with the numeric value next to it.
    drawNeedleGauge(g_rssiDisplay);
}

static void drawVolume() {
    tft.fillRect(0, VOL_Y, SCREEN_W, VOL_H, COL_BG);

    // Focus border — yellow in VOLUME mode, dim grey otherwise.
    uint16_t border = (currentMode == MODE_VOLUME) ? COL_FOCUS : COL_NOFOCUS;
    tft.drawRect(0, VOL_Y, SCREEN_W, VOL_H, border);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.setFreeFont(HEADER_FONT);
    tft.drawString("Vol", VOL_LABEL_X, VOL_LABEL_Y);

    uint8_t vol = radioGetVolume();
    int barFill = map(vol, 0, MAX_VOLUME, 0, VOL_BAR_W);
    tft.fillRect(VOL_BAR_X, VOL_BAR_Y, barFill, VOL_BAR_H, COL_VOL_FILL);
    tft.drawRect(VOL_BAR_X, VOL_BAR_Y, VOL_BAR_W, VOL_BAR_H, COL_METER_FRAME);

    char buf[4];
    snprintf(buf, sizeof(buf), "%u", vol);
    tft.drawString(buf, VOL_VAL_X, VOL_VAL_Y);
}

static void drawFooter() {
    tft.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, COL_BG);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(LABEL_FONT);

    char buf[48];
    uint16_t f = radioGetFrequency();
    snprintf(buf, sizeof(buf), "%s  %s  %u.%u MHz",
             FW_VERSION, POWER_SOURCE, f / 100, (f % 100) / 10);
    tft.drawString(buf, FOOTER_TXT_X, FOOTER_TXT_Y);
}

// ============================================================================
// Section 8: Needle S-meter
//
// Sprite-backed analog S-meter rendered in the left portion of the meter
// zone. The pivot sits below the visible sprite (see GAUGE_PIVOT_Y in
// ui_layout_tft.h) so only the top fan of a larger imaginary dial shows
// through — same silhouette as a vintage tuner's moving-coil meter.
//
// Flow:
//   1. initGauge()        — allocate the sprite once at boot.
//   2. drawMeter()        — called on DIRTY_METER: repaints RSSI/SNR chrome
//                           and calls drawNeedleGauge(g_rssiDisplay).
//   3. pumpNeedleAnimation() — called every loop(); advances the EMA of
//                           radioGetRssi() into g_rssiDisplay and repaints
//                           the sprite directly (bypassing the dirty-flag
//                           pipeline) when the needle has visibly moved.
// ============================================================================

static void initGauge() {
    // 16-bit colour sprite; ~14 KB heap. If this ever fails we fall back to
    // a black rectangle — no crash, just no needle.
    gauge.setColorDepth(16);
    gauge.createSprite(GAUGE_W, GAUGE_H);
    gauge.fillSprite(COL_GAUGE_BG);
}

// Map an S-meter value (in dBuV on the 0..RSSI_SCALE_MAX_DBUV scale) to a
// needle angle in radians. Angle convention here: 0 rad = straight up,
// positive rad = clockwise. The scale spans [-GAUGE_SWEEP_DEG, +GAUGE_SWEEP_DEG].
static float gaugeAngleRad(float dbuv) {
    float clamped = constrain(dbuv, 0.0f, (float)RSSI_SCALE_MAX_DBUV);
    float frac    = clamped / (float)RSSI_SCALE_MAX_DBUV;          // 0..1
    float deg     = -GAUGE_SWEEP_DEG + frac * 2.0f * GAUGE_SWEEP_DEG;
    return deg * (float)PI / 180.0f;
}

static void drawNeedleGauge(float dbuv) {
    gauge.fillSprite(COL_GAUGE_BG);

    // Tick marks every 10 dBuV. Inner radius -> outer radius, projected
    // from the pivot at the bottom. drawLine clips off-sprite coords, so
    // the pivot being below the sprite is fine.
    for (int tick = 0; tick <= (int)RSSI_SCALE_MAX_DBUV; tick += 10) {
        float a = gaugeAngleRad((float)tick);
        float s = sinf(a);
        float c = cosf(a);
        int x1 = GAUGE_PIVOT_X + (int)(GAUGE_R_INNER * s);
        int y1 = GAUGE_PIVOT_Y - (int)(GAUGE_R_INNER * c);
        int x2 = GAUGE_PIVOT_X + (int)(GAUGE_R_TICK  * s);
        int y2 = GAUGE_PIVOT_Y - (int)(GAUGE_R_TICK  * c);
        gauge.drawLine(x1, y1, x2, y2, COL_GAUGE_TICK);
    }

    // Needle — colour grades green -> yellow -> red as the signal rises so
    // the user gets an at-a-glance strength read without reading numerals.
    uint16_t needleCol = (dbuv < 20.0f) ? COL_NEEDLE_LOW
                       : (dbuv < 45.0f) ? COL_NEEDLE_MID
                                        : COL_NEEDLE_HIGH;
    float a = gaugeAngleRad(dbuv);
    float s = sinf(a);
    float c = cosf(a);
    int tipX = GAUGE_PIVOT_X + (int)(GAUGE_R_OUTER * s);
    int tipY = GAUGE_PIVOT_Y - (int)(GAUGE_R_OUTER * c);
    // Three parallel lines give a 3-px-wide needle without needing a
    // dedicated drawThickLine helper. Perpendicular offset = (cos, sin).
    int ox = (int)roundf(c);
    int oy = (int)roundf(s);
    gauge.drawLine(GAUGE_PIVOT_X,      GAUGE_PIVOT_Y,      tipX,      tipY,      needleCol);
    gauge.drawLine(GAUGE_PIVOT_X + ox, GAUGE_PIVOT_Y + oy, tipX + ox, tipY + oy, needleCol);
    gauge.drawLine(GAUGE_PIVOT_X - ox, GAUGE_PIVOT_Y - oy, tipX - ox, tipY - oy, needleCol);

    // Pivot cap — small white dot at the base of the needle. Only the top
    // half shows because the pivot lies below the sprite.
    gauge.fillCircle(GAUGE_PIVOT_X, GAUGE_PIVOT_Y, 4, COL_GAUGE_TICK);

    gauge.pushSprite(GAUGE_X, GAUGE_Y);
    g_rssiLastDrawn = dbuv;
}

static void pumpNeedleAnimation() {
    static unsigned long lastMs = 0;
    unsigned long now = millis();
    if (now - lastMs < NEEDLE_ANIM_MS) return;
    lastMs = now;

    // EMA toward the current radio RSSI. At α=0.25 and a 30 ms tick, the
    // needle reaches ~90 % of a step in ~8 ticks (~240 ms) — reads as a
    // mechanical needle settling rather than a pixel snap.
    float target = (float)radioGetRssi();
    g_rssiDisplay += NEEDLE_EMA_ALPHA * (target - g_rssiDisplay);

    // Only repaint when the needle has actually moved. Once it settles
    // within NEEDLE_EPSILON of the last-drawn value the gauge sits idle.
    if (fabsf(g_rssiDisplay - g_rssiLastDrawn) > NEEDLE_EPSILON) {
        drawNeedleGauge(g_rssiDisplay);
    }
}
