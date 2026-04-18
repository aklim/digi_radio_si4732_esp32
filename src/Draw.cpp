// ============================================================================
// Draw.cpp — port of the ATS-Mini widget-draw functions.
//
// Source: https://github.com/esp32-si4732/ats-mini/blob/main/ats-mini/Draw.cpp
//
// Adaptations for our fork:
//   * Upstream draws into a TFT_eSprite (`spr`) and pushes once per frame;
//     we render directly to the TFT reference (`g_tft`) provided at init.
//     No full-screen sprite — saves ~150 KB heap. Minor flicker trade-off
//     deemed acceptable until the Scan step, at which point a half-screen
//     sprite may come in for that zone specifically.
//   * Radio state comes from our radio.cpp API (radioGet*) instead of
//     ATS-Mini's globals (currentMode, currentFrequency, rx.*).
//   * Battery reads are stubbed (see Battery.cpp).
//   * drawFrequency drops the digit-input-highlight argument — no direct
//     digit-editing UI in this firmware yet.
//   * Unused helpers (BLE / WiFi indicators, long-station-name for EIBI,
//     digit-edit underline, save icon, scan graphs, theme editor preview)
//     are not ported in this step; they land with their respective
//     features in later steps.
// ============================================================================

#include "Draw.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "radio.h"
#include "Themes.h"
#include "Battery.h"

// Shared TFT handle — registered by main.cpp via drawInit(). Every widget
// function writes through this reference, mirroring ATS-Mini's `spr` usage
// but without the full-screen sprite.
static TFT_eSPI *g_tft = nullptr;

void drawInit(TFT_eSPI &tft) {
    g_tft = &tft;
}

// ---------------------------------------------------------------------------
// Battery icon + voltage readout.
//
// Upstream renders a cartoon battery with a fill prop to SOC plus either a
// charge glyph (bolt) or the voltage as text. Our stub always renders the
// "on battery" branch with the placeholder voltage from Battery.cpp.
// ---------------------------------------------------------------------------
bool drawBattery(int x, int y) {
    if (!g_tft) return false;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear — wipes any previous voltage-text artefact to the
    // left of the icon. Rect covers "-3.99V" (Font 2 ≈ 6 px/char) plus
    // the icon itself plus a tiny safety margin.
    s.fillRect(x - 52, y, 82, 16, TH.bg);

    // Outer frame (ATS-Mini uses 28 wide + 1 px contact pip on the right).
    s.drawSmoothRoundRect(x, y + 1, 3, 2, 28, 14, TH.batt_border, TH.bg);
    s.fillRect(x + 28, y + 5, 2, 6, TH.batt_border);

    // Inner fill proportional to SOC — 4 discrete levels matches upstream.
    uint8_t  soc     = batteryGetSocPercent();
    uint16_t fillCol = (soc < 25) ? TH.batt_low : TH.batt_full;
    int      w       = soc < 25 ? 6 : soc < 50 ? 12 : soc < 75 ? 18 : 24;
    s.fillRect(x + 2, y + 3, w, 10, fillCol);

    // Voltage label to the left of the icon (right-anchored at the battery).
    char volt[8];
    snprintf(volt, sizeof(volt), "%.2fV", batteryGetVolts());
    s.setTextDatum(MR_DATUM);
    s.setTextColor(TH.batt_voltage, TH.bg);
    s.drawString(volt, x - 2, y + 8, 2);
    s.setTextDatum(TL_DATUM);

    return true;  // has-voltage path — mirrors upstream's `has_voltage` flag
}

// ---------------------------------------------------------------------------
// Band tag + modulation box (e.g. "FM" + "[USB]").
//
// Upstream expects Orbitron_Light_24 to have been selected on the sprite
// before the call; we select it here ourselves so the caller (Layout-
// Default) does not need direct `tft` access.
// ---------------------------------------------------------------------------
extern const GFXfont Orbitron_Light_24;  // from TFT_eSPI bundled Custom fonts

void drawBandAndMode(const char *band, const char *mode, int x, int y) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear: band tag is TC-anchored at x with up to ~80 px
    // on each side, mode box extends ~60 px to the right. Combined rect
    // covers both with slack.
    s.fillRect(x - 80, y - 1, 220, 30, TH.bg);

    s.setFreeFont(&Orbitron_Light_24);
    s.setTextDatum(TC_DATUM);
    s.setTextColor(TH.band_text, TH.bg);
    uint16_t band_width = s.drawString(band, x, y);

    s.setTextDatum(TL_DATUM);
    s.setTextColor(TH.mode_text, TH.bg);
    uint16_t mode_width = s.drawString(mode, x + band_width / 2 + 12, y + 8, 2);

    s.drawSmoothRoundRect(x + band_width / 2 + 7, y + 7,
                          4, 4,
                          mode_width + 8, 17,
                          TH.mode_border, TH.bg);
}

// ---------------------------------------------------------------------------
// Big frequency readout using TFT_eSPI Font 7 (7-segment digital clock),
// plus a small unit label anchored to (ux, uy).
//
// Our signature drops upstream's `hl` argument because we do not have the
// digit-editing UI yet — the underline path will land with the Step-8
// keypad overlay.
// ---------------------------------------------------------------------------
void drawFrequency(uint32_t freq, int x, int y, int ux, int uy) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    const Band *band = radioGetCurrentBand();

    // Per-widget clear: Font 7 digit is ~32 px wide, we can have up to
    // 6 digits + decimal point right-anchored at x=250, so the left edge
    // reaches ~50. Height ~60 px covers digit (~50) + unit label + AM
    // fractional tail. Whole rect: (45..320, 35..105).
    s.fillRect(45, y - 25, 275, 70, TH.bg);

    s.setTextDatum(MR_DATUM);
    s.setTextColor(TH.freq_text, TH.bg);

    if (band->mode == MODE_FM) {
        // FM stores frequency in 10 kHz units → divide by 100 for MHz
        // with two decimal places. Example: 10240 -> 102.40.
        s.drawFloat(freq / 100.0f, 2, x, y, 7);
        s.setTextDatum(ML_DATUM);
        s.setTextColor(TH.funit_text, TH.bg);
        s.drawString("MHz", ux, uy);
    } else {
        // AM / MW / SW — native kHz. Render integer, then the upstream
        // decorative ".000" tail in Font 4 immediately to the right.
        s.drawNumber(freq, x, y, 7);
        s.setTextDatum(ML_DATUM);
        s.setTextColor(TH.freq_text, TH.bg);
        s.drawString(".000", 4 + x, 17 + y, 4);

        s.setTextColor(TH.funit_text, TH.bg);
        s.drawString("kHz", ux, uy);
    }
    s.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// RDS station name (PS). Centred at (x, y) in Font 4.
// ---------------------------------------------------------------------------
void drawStationName(const char *name, int x, int y) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear: Font 4 is ~11 px/char; PS is 8 chars max (~90 px
    // wide), centred on x. Rect 200 px wide, 22 px tall absorbs all PS
    // lengths including a cleanly blanking paint after sync drops.
    s.fillRect(x - 100, y, 200, 22, TH.bg);

    // Empty name ⇒ caller already cleared the zone (via the fillRect
    // above) and we are done. This lets Layout-Default call us every
    // frame unconditionally so sync drops wipe the old PS cleanly.
    if (!name || !name[0]) return;

    s.setTextDatum(TC_DATUM);
    s.setTextColor(TH.rds_text, TH.bg);
    s.drawString(name, x, y, 4);
    s.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// Top-edge S-meter.
//
// Icon: tiny speaker triangle at (x+1,y+1)..(x+11,y+1)..(x+6,y+6), with a
// vertical line down to the bar baseline. `strength` segments follow,
// 2 px wide × 12 px tall, 4 px pitch. First 10 segments in smeter_bar,
// the rest in smeter_bar_plus — matches ATS-Mini's colour convention.
// ---------------------------------------------------------------------------
void drawSMeter(int strength, int x, int y) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear: fills the whole 49-segment strip with the empty
    // colour so bars that were lit last frame but are dark this frame
    // drop out cleanly. Icon + line overlay immediately afterwards.
    s.fillRect(x, y, 15 + 49 * 4, 16, TH.bg);

    s.drawTriangle(x + 1, y + 1, x + 11, y + 1, x + 6, y + 6, TH.smeter_icon);
    s.drawLine(x + 6, y + 1, x + 6, y + 14, TH.smeter_icon);

    for (int i = 0; i < strength; i++) {
        uint16_t col = (i < 10) ? TH.smeter_bar : TH.smeter_bar_plus;
        s.fillRect(15 + x + (i * 4), 2 + y, 2, 12, col);
    }
}

// ---------------------------------------------------------------------------
// FM stereo indicator. When stereo is locked, paint a 2-px horizontal gap
// through the middle of the S-meter bar cluster — visually splits the bars
// into top and bottom halves. When mono, no-op (upstream intentionally
// leaves the "mono" indicator unstated).
// ---------------------------------------------------------------------------
void drawStereoIndicator(int x, int y, bool stereo) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    if (stereo) {
        s.fillRect(15 + x, 7 + y, 4 * 17 - 2, 2, TH.bg);
    }
}

// ---------------------------------------------------------------------------
// RDS RadioText (multi-line). Centred on x=160, one line every 17 px until
// (a) the text runs out or (b) y hits the caller-supplied ymax.
//
// Upstream walks a NUL-separated multi-string buffer (one RT group per
// segment). We pass a single RT string from the radio mirror, which
// renders as one page. EIBI / ProgramInfo fallback is not ported yet.
// ---------------------------------------------------------------------------
void drawRadioText(int y, int ymax) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear: RT can be up to ~40 chars wide in Font 2
    // (~6 px/char); clear a full-width strip so old pixels never linger
    // when the RT shrinks or clears.
    s.fillRect(0, y, 320, ymax - y, TH.bg);

    char rt[65];
    radioGetRdsRt(rt, sizeof(rt));
    if (!rt[0]) return;

    s.setTextDatum(TC_DATUM);
    s.setTextColor(TH.rds_text, TH.bg);
    if (y < ymax) {
        s.drawString(rt, 160, y, 2);
    }
    s.setTextDatum(TL_DATUM);
}
