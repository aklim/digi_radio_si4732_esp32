// ============================================================================
// Digital Radio Receiver Firmware — main entry (env:esp32dev)
// Hardware: ESP32 + Si4732 + Waveshare 2.8" TFT Touch Shield Rev 2.1
//           (ST7789V 240x320 + XPT2046 resistive touch) + Rotary Encoder
//
// Radio / encoder / menu / persistence live in radio.cpp / input.cpp /
// menu.cpp / persist.cpp. Only the top-level UI layout, backlight PWM, and
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
// The S-meter needle and modal menu additionally render through a
// TFT_eSprite for flicker-free updates.
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
#include "ui_layout.h"
#include "menu.h"
#include "persist.h"
#include "Themes.h"
#include "Draw.h"
#include "Scan.h"
#include "connectivity.h"

#include "version.h"

// Adafruit-GFX free fonts used across the UI. Aliased here so every draw
// function refers to a single source of truth and a theme / typography
// tweak is a one-line change. Each alias is the address of the font's
// GFXfont struct (the same value that Free_Fonts.h macros like FSSB12
// expand to in the TFT_eSPI examples).
// The big frequency digits now use TFT_eSPI's built-in Font 7 (a 7-segment
// digital-clock face, selected by passing `FONT_BIG` as the trailing arg
// to drawNumber / drawFloat in drawFrequency). The GFX free fonts below
// cover everything else.
//   HEADER_FONT  — "FM", "STEREO"/"MONO", "Vol"
//   LABEL_FONT   — section labels (RSSI, SNR, PS:, footer text)
//   VALUE_FONT   — numeric values next to the labels
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

// Firmware identity — the `asm volatile` anchor in setup() keeps the symbol
// alive against --gc-sections (the `used` attribute alone is not sufficient).
// FW_VERSION is rendered on-screen (header top-right + footer) so the
// running build is visible at a glance without a serial monitor.
extern "C" __attribute__((used)) const char FW_IDENTITY[] =
    "FW=" FW_VERSION " commit=" FW_GIT_COMMIT " built=" FW_BUILD_DATE;

// ============================================================================
// Section 2: Dirty-flags pipeline
// ============================================================================

// Single-bit repaint gate. ATS-Mini's pipeline redraws the whole screen
// into a sprite every frame — our port keeps the gate so we only spend
// pixels when something actually changed, but the payload is one
// drawLayoutDefault() call that repaints everything.
//
// DIRTY_FREQ / DIRTY_VOL aliases are kept so the input-handler call
// sites read naturally ("encoder rotated -> freq dirty"); they all flow
// into the same bit internally.
constexpr uint8_t DIRTY_ANY    = 1 << 0;
constexpr uint8_t DIRTY_FREQ   = DIRTY_ANY;
constexpr uint8_t DIRTY_RDS    = DIRTY_ANY;
constexpr uint8_t DIRTY_METER  = DIRTY_ANY;
constexpr uint8_t DIRTY_VOL    = DIRTY_ANY;
constexpr uint8_t DIRTY_FOOTER = DIRTY_ANY;
constexpr uint8_t DIRTY_HEADER = DIRTY_ANY;
constexpr uint8_t DIRTY_ALL    = DIRTY_ANY;

static uint8_t dirtyFlags = DIRTY_ALL;

static inline void markDirty(uint8_t bits) { dirtyFlags |= bits; }

// ============================================================================
// Section 3: Globals & state
// ============================================================================

static TFT_eSPI tft = TFT_eSPI();

// Full-screen 8-bit double-buffer sprite — ATS-Mini ports their 16-bit
// 320x170 sprite (~108 KB) onto this exact pattern; we drop to 8 bpp
// because 320x240x16 = 153 KB overflows our DRAM heap. At 8 bpp the
// sprite is 76 KB and the per-pixel RGB332 quantisation is visually
// acceptable for the themed solid-colour UI (most art is flat fills).
//
// g_useSprite flips false only if createSprite() itself fails, in
// which case updateDisplay() falls back to the per-widget rect-clear
// pipeline (flickery but functional).
static TFT_eSprite spr(&tft);
static bool        g_useSprite = false;

static AdjustMode currentMode = MODE_FREQUENCY;

// Last raw encoder value observed while the menu is open — used to compute
// the per-poll delta the menu layer wants. Reset to 0 in openMenu() because
// encoderSetBoundsForMenu() also resets the encoder to 0.
static long g_menuEncLast = 0;

// ============================================================================
// Section 4: Forward declarations
// ============================================================================

static void initBacklight();
static void initDisplay();
static void drawSplash();

static void updateDisplay();

static void handleEncoderRotation(long value);
static void toggleMode();
static void setMode(AdjustMode newMode);
static void handleTouch();
static void openMenu();
static void handleMenuClose();

// ============================================================================
// Section 5: setup() / loop()
// ============================================================================

void setup() {
    asm volatile("" : : "r"(FW_IDENTITY));

    Serial.begin(115200);
    Serial.println(F("Digital Radio (TFT) — starting up..."));

    // Force BT + WiFi into their lowest-power state before any other module
    // can reference those APIs. Persisted user preferences are re-applied
    // below after persistInit(); on fresh NVS both default to off.
    connectivityEarlyInit();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    initBacklight();
    initDisplay();
    tft.setTouch(TOUCH_CALIBRATION);
    drawSplash();

    // Allocate the flicker-free double-buffer as early as possible so
    // the DRAM heap is at its most contiguous (before Preferences,
    // SI4735 patch, RDS buffers, and the Core-0 radio task stack
    // stake their claims). 8 bpp keeps us under 80 KB.
    spr.setColorDepth(8);
    if (spr.createSprite(SCREEN_W, SCREEN_H)) {
        g_useSprite = true;
        drawInit(spr);
        Serial.printf("[main] frame sprite allocated (8bpp), free heap=%u\n",
                      (unsigned)ESP.getFreeHeap());
    } else {
        g_useSprite = false;
        drawInit(tft);
        Serial.println(F("[main] frame sprite FAILED — per-widget clears fallback"));
    }

    // Wait for the Si4732 RC reset circuit to release the chip.
    delay(500);

    // Load persisted state first so we can seed the band table before the
    // chip is powered up and avoid an extra retune on boot. First-boot
    // loads return 0 / default values.
    persistInit();

    // Restore the user's chosen palette before any draw runs so the very
    // first fillScreen picks up the right background. Out-of-range
    // indices (e.g. downgraded firmware with fewer themes) clamp to 0.
    {
        uint8_t t = persistLoadTheme();
        if (t >= (uint8_t)getTotalThemes()) t = 0;
        themeIdx = t;
    }

    // Seed the radio's band-table mutables directly from NVS. This must
    // happen *before* radioInit() so applyBandLocked() inside radioInit()
    // tunes to the right frequency on the first I2C write. We touch the
    // table synchronously — the radio mutex doesn't exist yet and there
    // is no task to race against.
    for (size_t i = 0; i < g_bandCount; i++) {
        uint16_t sf = persistLoadFrequency((uint8_t)i);
        if (sf) g_bands[i].currentFreq = sf;
    }

    // radioInit() creates the mutex, powers up the Si4732, applies the
    // current (index-0) band, then becomes thread-safe. Any radioSetXxx
    // call is safe from here on, even before radioStart() launches the
    // polling task.
    radioInit();

    // Seed the BW / AGC shadows from NVS now that the radio mutex exists.
    // These take effect on the next applyBandLocked() call (which happens
    // implicitly on every band switch, and explicitly inside radioSetBand
    // below when the saved band is non-zero). To make sure the boot band
    // itself picks them up we re-apply the current band after seeding.
    radioSeedBandwidthIdx(true,  persistLoadBandwidthFm());
    radioSeedBandwidthIdx(false, persistLoadBandwidthAm());
    radioSeedAgcIdx(true,  persistLoadAgcFm());
    radioSeedAgcIdx(false, persistLoadAgcAm());
    // radioInit() already ran setFM+BW+AGC with the *defaults* (shadows
    // hadn't been seeded yet); re-apply now so the chip reflects the saved
    // user selections. If radioSetBand() fires below with a non-zero saved
    // band, that path re-applies too and this call is redundant-but-cheap.
    radioApplyCurrentBand();

    // Restore persisted feature-enable flags. radioSetRdsEnabled takes the
    // radio mutex internally, so it must run after radioInit(). The BT/WiFi
    // setters physically (re-)init the respective radios — cheap no-op here
    // on fresh NVS since connectivityEarlyInit() already forced them off,
    // non-trivial if the user had them enabled before reboot.
    radioSetRdsEnabled(persistLoadRdsEnabled() != 0);
    connectivitySetBtEnabled(persistLoadBtEnabled() != 0);
    connectivitySetWifiEnabled(persistLoadWifiEnabled() != 0);

    // Apply saved band + volume now that the mutex is in place.
    uint8_t savedBand = persistLoadBand();
    if (savedBand != 0 && savedBand < g_bandCount) {
        radioSetBand(savedBand);
    }
    uint8_t savedVol = persistLoadVolume();
    if (savedVol > 0) radioSetVolume(savedVol);

    // Launch the radio polling task on Core 0. After this returns the UI
    // loop on Core 1 can freely call radioSetXxx / radioGetXxx; the task
    // drives signal + RDS polling at its own cadence.
    radioStart();

    encoderInit();
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());

    tft.fillScreen(COL_BG);
    dirtyFlags = DIRTY_ALL;
    Serial.println(F("Digital Radio (TFT) — ready."));
}

void loop() {
    // ---- Input --------------------------------------------------------------
    long encValue;
    bool rotated = encoderPollRotation(encValue);

    ButtonEvent btn = encoderPollButton();

    if (menuIsOpen()) {
        // Menu owns all input while visible. Rotation is fed as a delta
        // against the last observed raw value; click confirms / descends.
        if (rotated) {
            long delta = encValue - g_menuEncLast;
            g_menuEncLast = encValue;
            if (delta != 0) menuHandleRotation((int)delta);
        }
        if (btn == BTN_CLICK) {
            menuHandleClick();
        }
        if (btn == BTN_LONG_PRESS) {
            // Second long-press while menu is open = back out to main UI.
            menuClose();
        }
        if (!menuIsOpen()) handleMenuClose();
    } else if (scanIsActive()) {
        // Scan rules:
        //   RUN  — the chip is mid-sweep; swallow rotation (can't retune
        //          without scrambling samples). Click / long-press aborts
        //          and returns to the pre-scan frequency.
        //   DONE — the graph is held. Encoder rotation becomes normal
        //          tuning (drawScanGraphs() re-centers on radioGetFrequency()
        //          so the graph scrolls with the tune, matching ATS-Mini's
        //          CMD_SCAN branch, ats-mini.ino:825). Click exits the
        //          scan keeping the new tune; long-press aborts, restoring
        //          the original tune.
        if (scanIsDone()) {
            if (rotated) handleEncoderRotation(encValue);
            if (btn == BTN_CLICK) {
                // Hold current tune through the abort+exit sequence.
                uint16_t keepFreq = radioGetFrequency();
                scanAbort();
                radioSetFrequency(keepFreq);
                persistSaveFrequency(radioGetBandIdx(), keepFreq);
                markDirty(DIRTY_ALL);
            } else if (btn == BTN_LONG_PRESS) {
                scanAbort();
                markDirty(DIRTY_ALL);
            }
        } else {
            if (btn == BTN_CLICK || btn == BTN_LONG_PRESS) {
                scanAbort();
                markDirty(DIRTY_ALL);
            }
        }
    } else {
        if (rotated) handleEncoderRotation(encValue);
        if (btn == BTN_CLICK)      toggleMode();
        if (btn == BTN_LONG_PRESS) openMenu();
    }

    // ---- Radio polling / main UI --------------------------------------------
    // Skip polling work while the menu is open — the main zones are hidden
    // so repainting them costs pixels nobody sees. Signal + RDS state
    // "catches up" on menu close because the poll intervals are self-
    // rate-limited and will fire on the next loop iteration after close.
    if (!menuIsOpen()) {
        // Advance the bandscope sweep one sample per loop iteration. Each
        // scanTick takes ~60-80 ms (tune + settle + quality read) so the
        // full 200-sample sweep completes in ~15 s. Paint after every
        // sample so the graph fills in progressively; always repaint on
        // the transition-to-DONE tick too (scanTick() returns false for
        // that one but the last sample still needs to show).
        if (scanIsActive()) {
            scanTick();
            markDirty(DIRTY_ALL);
        }
        if (radioPollSignal()) {
            markDirty(DIRTY_METER | DIRTY_HEADER);
        }
        if (radioPollRds()) {
            markDirty(DIRTY_RDS);
        }
        handleTouch();
        updateDisplay();
    } else if (menuTakeDirty()) {
        menuDraw(tft);
    }
}

// ============================================================================
// Section 6: Input handling
// ============================================================================

static void handleEncoderRotation(long value) {
    if (currentMode == MODE_FREQUENCY) {
        const Band* b = radioGetCurrentBand();
        uint16_t newFreq = (uint16_t)(value * b->step);
        if (newFreq < b->minFreq) newFreq = b->minFreq;
        if (newFreq > b->maxFreq) newFreq = b->maxFreq;
        if (newFreq != radioGetFrequency()) {
            radioSetFrequency(newFreq);
            persistSaveFrequency(radioGetBandIdx(), newFreq);
            // Tune clears RDS mirrors; repaint the RDS zone so old text goes
            // away immediately instead of waiting for the next 200 ms poll.
            markDirty(DIRTY_FREQ | DIRTY_FOOTER | DIRTY_RDS);
        }
    } else {
        uint8_t newVol = (uint8_t)value;
        if (newVol > MAX_VOLUME) newVol = MAX_VOLUME;
        if (newVol != radioGetVolume()) {
            radioSetVolume(newVol);
            persistSaveVolume(newVol);
            markDirty(DIRTY_VOL);
        }
    }
}

static void setMode(AdjustMode newMode) {
    if (newMode == currentMode) return;
    currentMode = newMode;
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());
    // Sidebar reads getAdjustMode() to bold the Vol row in VOLUME mode;
    // repaint both zones so the highlight appears/disappears immediately.
    markDirty(DIRTY_FREQ | DIRTY_VOL);
    Serial.print(F("Mode: "));
    Serial.println((currentMode == MODE_FREQUENCY) ? F("FREQUENCY") : F("VOLUME"));
}

static void toggleMode() {
    setMode(currentMode == MODE_FREQUENCY ? MODE_VOLUME : MODE_FREQUENCY);
}

AdjustMode getAdjustMode() {
    return currentMode;
}

// Open the long-press menu. Encoder is reconfigured for menu semantics
// (wide bounds, no acceleration, reset to 0) so rotation deltas match
// 1 detent = 1 item. The subsequent close reverts via handleMenuClose().
static void openMenu() {
    Serial.println(F("Menu: open"));
    encoderSetBoundsForMenu();
    g_menuEncLast = 0;
    menuOpen();
}

// Called right after menuClose() so we re-establish the main-UI encoder
// bounds and force a full repaint (the menu overwrote every pixel, so no
// partial refresh will do).
static void handleMenuClose() {
    Serial.println(F("Menu: close"));
    encoderSetBoundsForMode(currentMode, radioGetFrequency(), radioGetVolume());
    dirtyFlags = DIRTY_ALL;
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

    // TFT_eSPI's getTouch applies the stored calibration but does not
    // rotate the result. Our calibration was captured in rotation 0
    // (native 240×320 portrait). We now run in rotation 3 (landscape
    // 320×240) so transform the returned rotation-0 coords into the
    // display's current logical frame:
    //   (x_disp, y_disp) = (y_raw, PANEL_W_NATIVE - 1 - x_raw)
    uint16_t rawX = tx, rawY = ty;
    tx = rawY;
    ty = PANEL_W_NATIVE - 1 - rawX;

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
    // Rotation 3 = landscape (320×240). Matches ATS-Mini's assumed canvas
    // and is the only rotation this firmware supports from here on — the
    // previous DISPLAY_FLIPPED knob retires.
    tft.setRotation(3);
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

// ============================================================================
// Section 7: Full-screen repaint
// ============================================================================

static void updateDisplay() {
    if (!dirtyFlags) return;
    if (g_useSprite) {
        // Flicker-free: clear the off-screen buffer, repaint every
        // widget into it, push the whole frame once. Atomic update,
        // no tear or flash between fillRect and redraw of any widget.
        spr.fillSprite(TH.bg);
        drawLayoutDefault();
        spr.pushSprite(0, 0);
    } else {
        // Fallback (sprite alloc failed): per-widget fillRect inside
        // each draw function prevents inter-widget flicker but the
        // single changing widget still flashes on update.
        drawLayoutDefault();
    }
    dirtyFlags = 0;
}
