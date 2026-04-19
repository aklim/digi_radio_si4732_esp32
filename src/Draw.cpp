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
#include "Scan.h"

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
    // the icon itself plus the stepped positive terminal out to x+30.
    s.fillRect(x - 52, y, 84, 16, TH.bg);

    // Outer frame — 1:1 with ATS-Mini (Battery.cpp:77). drawRoundRect
    // gives a single-pixel outline with sharp r=3 corners. The earlier
    // port used drawSmoothRoundRect whose AA halo made the 10-px-tall
    // fill appear to bleed through the corners.
    s.drawRoundRect(x, y + 1, 28, 14, 3, TH.batt_border);

    // Positive terminal: two stacked lines form a stepped nubbin that
    // visually connects to the frame. Previous port used a flat
    // fillRect(x+28, y+5, 2, 6) which read as a detached block next to
    // the frame (the "cut-out" the user spotted).
    s.drawLine(x + 29, y + 5, x + 29, y + 10, TH.batt_border);
    s.drawLine(x + 30, y + 6, x + 30, y + 9,  TH.batt_border);

    // Inner fill proportional to SOC — 4 discrete levels, matching
    // ATS-Mini's 6/12/18/24 px widths. fillRoundRect(r=2) keeps the
    // fill corners inside the frame's rounded corners instead of
    // spilling into the AA zone.
    uint8_t  soc     = batteryGetSocPercent();
    uint16_t fillCol = (soc < 25) ? TH.batt_low : TH.batt_full;
    int      w       = soc < 25 ? 6 : soc < 50 ? 12 : soc < 75 ? 18 : 24;
    s.fillRoundRect(x + 2, y + 3, w, 10, 2, fillCol);

    // Voltage label to the left of the icon (right-anchored at the battery).
    char volt[8];
    snprintf(volt, sizeof(volt), "%.2fV", batteryGetVolts());
    s.setTextDatum(TR_DATUM);
    s.setTextColor(TH.batt_voltage, TH.bg);
    s.drawString(volt, x - 3, y, 2);
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

    // NOTE: no per-widget clear here — matches ATS-Mini (Draw.cpp:117).
    // The earlier port used `fillRect(x - 80, y - 1, 220, 30)` which
    // with BAND_OFFSET_X=150 ran to x=290 and wiped the left 6 px of
    // the battery frame + the whole voltage label every frame. The
    // fullscreen sprite clear in updateDisplay() handles erase for us.

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

    // NOTE: no per-widget clear here — matches ATS-Mini. The caller
    // (drawLayoutDefault via main.cpp) already wipes the full sprite with
    // `spr.fillSprite(TH.bg)` at the start of every frame, so a fillRect
    // over y=120..169 would just redo that work. It used to live here
    // but was erasing the bottom edge of the sidebar info box (which
    // runs to y=129, nine pixels into the scale zone).

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
static void drawInfo(int x, int y, int sx, bool volFocus) {
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
    s.drawString(radioGetBandwidthDesc(), 48 + x, row - 2 * 16, 2);

    // AGC: "On" when AGC active, "Att:NN" when manual attenuator armed.
    // Upstream switches the row label itself, not just the value, when
    // attenuation is in use — replicate that for parity.
    if (radioAgcIsOn()) {
        s.drawString("AGC:", 6 + x, row - 1 * 16, 2);
        s.drawString("On",  48 + x, row - 1 * 16, 2);
    } else {
        char attBuf[4];
        snprintf(attBuf, sizeof(attBuf), "%02u", (unsigned)radioGetAgcAttIdx());
        s.drawString("Att:", 6 + x, row - 1 * 16, 2);
        s.drawString(attBuf, 48 + x, row - 1 * 16, 2);
    }

    // Volume — real value. Upstream has mute/squelch states that change
    // the colour + text; those join in a later step.
    //
    // volFocus: when the encoder is bound to VOLUME, render the row in the
    // FreeSansBold9pt7b GFX free font so it is visibly bolder than the
    // surrounding Font 2 rows. Font 2 is a TFT_eSPI bitmap with no bold
    // sibling; double-drawing with a 1 px offset just stretches glyphs
    // horizontally, it does not look bold. Switching to a bold GFX font is
    // the canonical Bodmer-recommended approach. With ML_DATUM already set
    // at the top of drawInfo, both fonts vertically centre on the same y,
    // so the row's baseline does not shift when the highlight toggles.
    if (volFocus) {
        s.setFreeFont(&FreeSansBold9pt7b);
        s.drawString("Vol:", 6 + x, row + 0 * 16);
        s.drawNumber(radioGetVolume(), 48 + x, row + 0 * 16);
        s.setTextFont(2);  // restore bitmap Font 2 for the rows below
    } else {
        s.drawString("Vol:", 6 + x, row + 0 * 16, 2);
        s.drawNumber(radioGetVolume(), 48 + x, row + 0 * 16, 2);
    }

    // PI: 4-hex-digit station ID from the RDS mirror. Shows "--" when
    // the radio is not FM or PI has not decoded yet.
    uint16_t pi = radioGetRdsPi();
    if (pi && band->mode == MODE_FM) {
        char piBuf[8];
        snprintf(piBuf, sizeof(piBuf), "%04X", pi);
        s.drawString("PI:", 6 + x, row + 1 * 16, 2);
        s.drawString(piBuf, 48 + x, row + 1 * 16, 2);
    } else {
        s.drawString("PI:", 6 + x, row + 1 * 16, 2);
        s.drawString("--", 48 + x, row + 1 * 16, 2);
    }

    s.drawString("Time:", 6 + x, row + 2 * 16, 2);
    s.drawString("--:--", 48 + x, row + 2 * 16, 2);

    s.setTextDatum(TL_DATUM);
}

// ---------------------------------------------------------------------------
// Bandscope graph — plots SNR (yellow) and RSSI (green) lines on top of
// a dotted-grid background across the 41 visible ticks of the scale
// zone. Ported 1:1 from ats-mini/Draw.cpp; freq units match the scale
// (freq/10 == 100 kHz tick on FM, 10 kHz tick on AM/MW/SW).
// ---------------------------------------------------------------------------
void drawScanGraphs(uint32_t freq) {
    if (!g_tft) return;
    TFT_eSPI &s = *g_tft;

    // Per-widget clear covers the same band the scale uses.
    s.fillRect(0, 120, 320, 50, TH.bg);

    int16_t offset = (freq % 10) / 10.0f * 8;
    freq = freq / 10 - 20;

    const Band *band    = radioGetCurrentBand();
    uint32_t    minFreq = band->minFreq / 10;
    uint32_t    maxFreq = band->maxFreq / 10;

    for (int i = 0; i < 41; i++, freq++) {
        int16_t x = i * 8 - offset;
        if (freq < minFreq || freq > maxFreq) continue;

        // Vertical grid column every 5 tick positions.
        if ((freq % 5) == 0) {
            for (int y = 0; y < 42; y += 2) {
                s.drawPixel(x, 169 - y, TH.scan_grid);
            }
        }

        if ((freq + 1) > maxFreq) continue;

        // Horizontal dotted gridlines every 10 vertical pixels.
        for (int xd = x; xd < (x + 8); xd += 2) {
            s.drawPixel(xd, 169 -  0, TH.scan_grid);
            s.drawPixel(xd, 169 - 10, TH.scan_grid);
            s.drawPixel(xd, 169 - 20, TH.scan_grid);
            s.drawPixel(xd, 169 - 30, TH.scan_grid);
            s.drawPixel(xd, 169 - 40, TH.scan_grid);
        }

        // SNR line (yellow / theme scan_snr).
        int snr1 = 40 * scanGetSNR(freq * 10);
        int snr2 = 40 * scanGetSNR((freq + 1) * 10);
        s.drawLine(x, 169 - snr1, x + 8, 169 - snr2, TH.scan_snr);

        // RSSI line (green / theme scan_rssi).
        int rssi1 = 40 * scanGetRSSI(freq * 10);
        int rssi2 = 40 * scanGetRSSI((freq + 1) * 10);
        s.drawLine(x, 169 - rssi1, x + 8, 169 - rssi2, TH.scan_rssi);
    }

    // Centre-frequency pointer (sits above the grid so it always reads).
    s.fillTriangle(156, 125, 160, 130, 164, 125, TH.scale_pointer);
    s.drawLine(160, 130, 160, 169, TH.scale_pointer);
}

void drawSideBar(int x, int y, int sx, bool volFocus) {
    // Upstream switches between a dozen overlay screens (Menu, Settings,
    // Step, BW, Theme, ...) based on currentCmd. Our firmware routes menu
    // UI through the separate modal pipeline in menu.cpp, so the sidebar
    // only ever shows the info box here. Overlay states can be ported
    // later if we also move menu.cpp under the drawLayoutDefault umbrella.
    drawInfo(x, y, sx, volFocus);
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
