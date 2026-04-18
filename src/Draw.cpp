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
// ---------------------------------------------------------------------------
// Band scale — horizontal ruler under the frequency readout with tick
// marks + numeric labels at every 100 kHz. Ported 1:1 from ATS-Mini
// with our radio API substituted for currentMode / getCurrentBand().
// ---------------------------------------------------------------------------
void drawScale(uint32_t freq) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Wipe the scale zone first so tick marks from a previous band /
    // freq don't linger.
    s.fillRect(0, 120, 320, 50, TH.bg);

    // Centre pointer (triangle + vertical line dropping into the ticks).
    s.fillTriangle(156, 120, 160, 130, 164, 120, TH.scale_pointer);
    s.drawLine(160, 130, 160, 169, TH.scale_pointer);

    s.setTextDatum(MC_DATUM);
    s.setTextColor(TH.scale_text, TH.bg);

    // Extra frequencies drawn beyond the screen edges so ticks do not
    // pop in / out as the tune sweeps.
    int16_t slack = 3;

    // Pixel-offset of the centre tick so the band glides smoothly as
    // frequency crosses 10 kHz boundaries.
    int16_t offset = ((freq % 10) / 10.0f + slack) * 8;

    // First tick to render — 20 ticks to the left of centre, plus slack.
    freq = freq / 10 - 20 - slack;

    const Band *band    = radioGetCurrentBand();
    uint32_t    minFreq = band->minFreq / 10;
    uint32_t    maxFreq = band->maxFreq / 10;

    for (int i = 0; i < (slack + 41 + slack); i++, freq++) {
        int16_t x = i * 8 - offset;
        if (freq < minFreq || freq > maxFreq) continue;

        uint16_t lineColor =
            (i == 20) && (!offset || (!(freq % 5) && offset == 1))
                ? TH.scale_pointer
                : TH.scale_line;

        if ((freq % 10) == 0) {
            s.drawLine(x, 169, x, 150, lineColor);
            s.drawLine(x + 1, 169, x + 1, 150, lineColor);
            if (band->mode == MODE_FM) {
                s.drawFloat(freq / 10.0f, 1, x, 140, 2);
            } else if (freq >= 100) {
                s.drawFloat(freq / 100.0f, 3, x, 140, 2);
            } else {
                s.drawNumber(freq * 10, x, 140, 2);
            }
        } else if ((freq % 5) == 0) {
            s.drawLine(x, 169, x, 155, lineColor);
            s.drawLine(x + 1, 169, x + 1, 155, lineColor);
        } else {
            s.drawLine(x, 169, x, 160, lineColor);
        }
    }
    s.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// Left sidebar — info box with Step / BW / AGC / Vol / PI / Time rows.
// drawInfo is the default content (no overlay menu active). BW / AGC /
// PI rows currently show "--" placeholders; they light up in Step 6 when
// radio.cpp grows the matching getters.
// ---------------------------------------------------------------------------
static void drawInfo(int x, int y, int sx) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    s.setTextDatum(ML_DATUM);
    s.setTextColor(TH.box_text, TH.box_bg);

    // Rounded border + inner fill, same geometry upstream uses (box is
    // 76+sx wide, 110 tall, 2-px double-stroke border).
    s.fillSmoothRoundRect(1 + x, 1 + y, 76 + sx, 110, 4, TH.box_border);
    s.fillSmoothRoundRect(2 + x, 2 + y, 74 + sx, 108, 4, TH.box_bg);

    // Upstream anchors every row at (y + 64) and steps ±16 per slot.
    // Rows -3..+2 → y=16, 32, 48, 64, 80, 96 (relative to box origin).
    const int row = 64 + y;

    // Step: native-unit value → short description.
    char stepBuf[10];
    const Band *band = radioGetCurrentBand();
    uint16_t stepUnits = band->step;  // 10 kHz units on FM, 1 kHz on AM/SW
    if (band->mode == MODE_FM) {
        snprintf(stepBuf, sizeof(stepBuf), "%luk", (unsigned long)stepUnits * 10u);
    } else {
        snprintf(stepBuf, sizeof(stepBuf), "%luk", (unsigned long)stepUnits);
    }
    s.drawString("Step:", 6 + x, row - 3 * 16, 2);
    s.drawString(stepBuf, 48 + x, row - 3 * 16, 2);

    s.drawString("BW:",  6 + x, row - 2 * 16, 2);
    s.drawString("--",  48 + x, row - 2 * 16, 2);

    s.drawString("AGC:", 6 + x, row - 1 * 16, 2);
    s.drawString("--",  48 + x, row - 1 * 16, 2);

    // Volume — real value. Upstream has mute/squelch states that change
    // the colour + text; those join in a later step.
    s.drawString("Vol:", 6 + x, row + 0 * 16, 2);
    s.drawNumber(radioGetVolume(), 48 + x, row + 0 * 16, 2);

    s.drawString("PI:",  6 + x, row + 1 * 16, 2);
    s.drawString("--",  48 + x, row + 1 * 16, 2);

    s.drawString("Time:", 6 + x, row + 2 * 16, 2);
    s.drawString("--:--", 48 + x, row + 2 * 16, 2);

    s.setTextDatum(TL_DATUM);
}

void drawSideBar(int x, int y, int sx) {
    // Upstream switches between a dozen overlay screens (Menu, Settings,
    // Step, BW, Theme, ...) based on currentCmd. Our firmware routes menu
    // UI through the separate modal pipeline in menu.cpp, so the sidebar
    // only ever shows the info box here. Overlay states can be ported
    // later if we also move menu.cpp under the drawLayoutDefault umbrella.
    drawInfo(x, y, sx);
}

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
