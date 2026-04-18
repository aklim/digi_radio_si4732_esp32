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
//   Touch CS     : GPIO 17 (XPT2046)
//
// This file is a minimal boot/tune/render skeleton — S-meter, RDS, stereo,
// SNR, focus borders, and touch are layered on in subsequent commits.
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>

#include "radio.h"
#include "input.h"
#include "ui_layout_tft.h"

#include "version.h"

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

// Firmware identity — same pattern as main.cpp; the `asm volatile` anchor in
// setup() keeps the symbol alive against --gc-sections. Unlike the OLED,
// the TFT variant renders FW_VERSION on-screen (footer + header), since the
// 240x320 canvas has the room and the user asked for it.
extern "C" __attribute__((used)) const char FW_IDENTITY[] =
    "FW=" FW_VERSION " commit=" FW_GIT_COMMIT " built=" FW_BUILD_DATE;

// ============================================================================
// Section 2: Globals & state
// ============================================================================

static TFT_eSPI tft = TFT_eSPI();

static AdjustMode currentMode        = MODE_FREQUENCY;
static bool       displayNeedsUpdate = true;

// ============================================================================
// Section 3: Forward declarations
// ============================================================================

static void initBacklight();
static void initDisplay();
static void drawSplash();
static void updateDisplay();
static void drawHeader();
static void drawFrequency();
static void drawVolume();
static void drawFooter();

static void handleEncoderRotation(long value);
static void toggleMode();

// ============================================================================
// Section 4: setup() / loop()
// ============================================================================

void setup() {
    asm volatile("" : : "r"(FW_IDENTITY));

    Serial.begin(115200);
    Serial.println(F("Digital Radio (TFT) — starting up..."));

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    initBacklight();
    initDisplay();
    drawSplash();

    // Wait for the Si4732 RC reset circuit to release the chip.
    delay(500);

    radioInit();
    encoderInit();
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());

    tft.fillScreen(COL_BG);
    displayNeedsUpdate = true;
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
        // In this minimal commit the signal poll does not yet drive any UI
        // element (S-meter lands in commit 3). Still call it to populate the
        // cache and flip displayNeedsUpdate in case a later layer uses it.
    }
    updateDisplay();
}

// ============================================================================
// Section 5: Input handling
// ============================================================================

static void handleEncoderRotation(long value) {
    if (currentMode == MODE_FREQUENCY) {
        uint16_t newFreq = (uint16_t)(value * FM_FREQ_STEP);
        if (newFreq < FM_FREQ_MIN) newFreq = FM_FREQ_MIN;
        if (newFreq > FM_FREQ_MAX) newFreq = FM_FREQ_MAX;
        if (newFreq != radioGetFrequency()) {
            radioSetFrequency(newFreq);
            displayNeedsUpdate = true;
        }
    } else {
        uint8_t newVol = (uint8_t)value;
        if (newVol > MAX_VOLUME) newVol = MAX_VOLUME;
        if (newVol != radioGetVolume()) {
            radioSetVolume(newVol);
            displayNeedsUpdate = true;
        }
    }
}

static void toggleMode() {
    currentMode = (currentMode == MODE_FREQUENCY) ? MODE_VOLUME : MODE_FREQUENCY;
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());
    displayNeedsUpdate = true;
    Serial.print(F("Mode: "));
    Serial.println((currentMode == MODE_FREQUENCY) ? F("FREQUENCY") : F("VOLUME"));
}

// ============================================================================
// Section 6: Display
// ============================================================================

static void initBacklight() {
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_RES_BITS);
    ledcAttachPin(TFT_BL, BL_LEDC_CHANNEL);
    ledcWrite(BL_LEDC_CHANNEL, BL_DEFAULT_DUTY);
}

static void initDisplay() {
    tft.init();
    tft.setRotation(0);       // portrait 240x320
    tft.fillScreen(COL_BG);
}

static void drawSplash() {
    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, COL_BG);
    tft.drawString("Digital Radio", SCREEN_W / 2, 80, FONT_LABEL);
    tft.setTextColor(COL_VERSION, COL_BG);
    tft.drawString(FW_VERSION, SCREEN_W / 2, 120, FONT_SMALL);
    tft.setTextColor(TFT_DARKGREY, COL_BG);
    tft.drawString("Initializing...", SCREEN_W / 2, 160, FONT_SMALL);
    tft.setTextDatum(TL_DATUM);
}

// For the minimal commit we do a full repaint-when-dirty; commit 3 will
// split the pipeline into per-zone dirty flags so unchanged areas are not
// re-touched.
static void updateDisplay() {
    if (!displayNeedsUpdate) return;
    displayNeedsUpdate = false;

    drawHeader();
    drawFrequency();
    drawVolume();
    drawFooter();
}

static void drawHeader() {
    tft.fillRect(0, HEADER_Y, SCREEN_W, HEADER_H, COL_HEADER_BG);
    tft.setTextColor(COL_HEADER_TXT, COL_HEADER_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("FM", HEADER_MODE_X, HEADER_MODE_Y, FONT_LABEL);

    // Right-aligned version string.
    tft.setTextColor(COL_VERSION, COL_HEADER_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(FW_VERSION, HEADER_VER_R, HEADER_VER_Y, FONT_SMALL);
    tft.setTextDatum(TL_DATUM);
}

static void drawFrequency() {
    tft.fillRect(0, FREQ_Y, SCREEN_W, FREQ_H, COL_BG);
    tft.setTextColor(COL_FREQ_TXT, COL_BG);

    uint16_t freq = radioGetFrequency();
    char buf[8];
    snprintf(buf, sizeof(buf), "%u.%u", freq / 100, (freq % 100) / 10);

    tft.setTextDatum(MC_DATUM);
    tft.drawString(buf, SCREEN_W / 2 - 20, FREQ_TEXT_Y + FREQ_H / 2 - 12, FONT_BIG);
    tft.setTextDatum(TL_DATUM);

    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.drawString("MHz", FREQ_UNIT_X, FREQ_UNIT_Y, FONT_LABEL);
}

static void drawVolume() {
    tft.fillRect(0, VOL_Y, SCREEN_W, VOL_H, COL_BG);

    tft.setTextColor(COL_LABEL_TXT, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Vol", VOL_LABEL_X, VOL_LABEL_Y, FONT_LABEL);

    uint8_t vol = radioGetVolume();
    int barFill = map(vol, 0, MAX_VOLUME, 0, VOL_BAR_W);
    tft.fillRect(VOL_BAR_X, VOL_BAR_Y, barFill, VOL_BAR_H, COL_VOL_FILL);
    tft.drawRect(VOL_BAR_X, VOL_BAR_Y, VOL_BAR_W, VOL_BAR_H, COL_METER_FRAME);

    char buf[4];
    snprintf(buf, sizeof(buf), "%u", vol);
    tft.drawString(buf, VOL_VAL_X, VOL_VAL_Y, FONT_LABEL);
}

static void drawFooter() {
    tft.fillRect(0, FOOTER_Y, SCREEN_W, FOOTER_H, COL_BG);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    tft.setTextDatum(TL_DATUM);

    char buf[48];
    snprintf(buf, sizeof(buf), "%s  %s", FW_VERSION, POWER_SOURCE);
    tft.drawString(buf, FOOTER_TXT_X, FOOTER_TXT_Y, FONT_SMALL);
}
