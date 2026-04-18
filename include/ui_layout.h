// ============================================================================
// ui_layout.h — UI coordinates, sizes, colors, and fonts for the Waveshare
//               2.8" ST7789V TFT (240x320 portrait, rotation 0).
//
// Keeping magic numbers out of main.cpp means docs/display_tft.md can
// reference these constants by name and the two stay in sync as the layout
// evolves. Zones are stacked vertically top-to-bottom:
//
//   y=0   +--------------------------------+
//         |  header (mode + stereo + ver)  |  28 px
//   y=32  +--------------------------------+
//         |                                |
//         |       FREQUENCY  (FONT7)       |  108 px
//         |                                |
//   y=140 +--------------------------------+
//         |  RDS  (PS + RT)                |  68 px
//   y=208 +--------------------------------+
//         |  S-meter + SNR + stereo dot    |  48 px
//   y=256 +--------------------------------+
//         |  Volume bar + number           |  48 px
//   y=304 +--------------------------------+
//         |  footer (version + source)     |  16 px
//   y=320 +--------------------------------+
// ============================================================================

#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include <stdint.h>
#include <TFT_eSPI.h>   // for TFT_* color macros

// --- Panel -----------------------------------------------------------------
// Landscape orientation (rotation 3). Native panel is 240×320 portrait; the
// driver reports 320×240 after rotate. This mirrors the ATS-Mini UI which
// assumes a landscape canvas for its sidebar + big frequency layout.
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

// Native (rotation-0) panel dimensions — needed by the touch transform
// because TFT_eSPI's XPT2046 calibration was captured in rotation 0 and
// getTouch() does not re-rotate on its own.
constexpr int16_t PANEL_W_NATIVE = 240;
constexpr int16_t PANEL_H_NATIVE = 320;

// --- Zones (y/height) ------------------------------------------------------
constexpr int16_t HEADER_Y = 0,   HEADER_H = 28;
constexpr int16_t FREQ_Y   = 32,  FREQ_H   = 108;
constexpr int16_t RDS_Y    = 140, RDS_H    = 68;
constexpr int16_t METER_Y  = 208, METER_H  = 48;
constexpr int16_t VOL_Y    = 256, VOL_H    = 48;
constexpr int16_t FOOTER_Y = 304, FOOTER_H = 16;

// --- Colors ----------------------------------------------------------------
constexpr uint16_t COL_BG          = TFT_BLACK;
constexpr uint16_t COL_HEADER_BG   = TFT_NAVY;
constexpr uint16_t COL_HEADER_TXT  = TFT_WHITE;
constexpr uint16_t COL_FREQ_TXT    = TFT_CYAN;
constexpr uint16_t COL_LABEL_TXT   = TFT_WHITE;
constexpr uint16_t COL_DIM_TXT     = TFT_DARKGREY;
constexpr uint16_t COL_METER_FILL  = TFT_GREEN;
constexpr uint16_t COL_METER_FRAME = TFT_WHITE;
constexpr uint16_t COL_VOL_FILL    = TFT_SKYBLUE;
constexpr uint16_t COL_FOCUS       = TFT_YELLOW;
constexpr uint16_t COL_NOFOCUS     = TFT_DARKGREY;
constexpr uint16_t COL_STEREO_ON   = TFT_GREEN;
constexpr uint16_t COL_STEREO_OFF  = TFT_DARKGREY;
constexpr uint16_t COL_VERSION     = TFT_YELLOW;

// --- Fonts (TFT_eSPI built-ins loaded via User_Setup.h) --------------------
// Legacy bitmap-font aliases, kept as fallbacks during the FreeFonts
// rollout. main.cpp now routes every drawString() through the
// Adafruit-GFX free fonts (FSSB12, FSS9, FSSB9, FMB24) declared near the
// draw functions — these constants are retained for any code that still
// needs a bitmap-font identifier and for quick A/B comparison.
//   FONT7 = 7-segment style, numeric + '.' only.
//   FONT4 = 26 px sans — labels, volume number, SNR number.
//   FONT2 = 16 px sans — footer, RDS RadioText body.
constexpr uint8_t FONT_BIG   = 7;
constexpr uint8_t FONT_LABEL = 4;
constexpr uint8_t FONT_SMALL = 2;

// --- Header ----------------------------------------------------------------
constexpr int16_t HEADER_MODE_X  = 6;
constexpr int16_t HEADER_MODE_Y  = HEADER_Y + 4;
constexpr int16_t HEADER_ST_X    = 56;   // "STEREO" / "MONO"
constexpr int16_t HEADER_ST_Y    = HEADER_Y + 6;
constexpr int16_t HEADER_VER_R   = SCREEN_W - 6;  // right-aligned version
constexpr int16_t HEADER_VER_Y   = HEADER_Y + 8;

// --- Frequency zone --------------------------------------------------------
// FONT7 is ~48 px tall at size 1, ~75 px at size 1 in TFT_eSPI terms. We
// draw it centred horizontally; the MHz unit label sits below-right.
constexpr int16_t FREQ_TEXT_Y   = FREQ_Y + 18;
constexpr int16_t FREQ_UNIT_X   = 176;
constexpr int16_t FREQ_UNIT_Y   = FREQ_Y + FREQ_H - 24;

// --- RDS zone --------------------------------------------------------------
constexpr int16_t RDS_PS_X   = 4;
constexpr int16_t RDS_PS_Y   = RDS_Y + 6;    // FONT4 label + value
constexpr int16_t RDS_RT_X   = 4;
constexpr int16_t RDS_RT_Y   = RDS_Y + 38;   // FONT2, truncated to screen width
constexpr uint8_t RDS_RT_MAX_CHARS = 38;     // FONT2 fits ~38 chars at 240 px

// --- S-meter zone ----------------------------------------------------------
// The first row of the meter zone now hosts an analog needle gauge rendered
// through a TFT_eSprite (see main.cpp, section "Needle S-meter"). The
// numeric dBuV value and the SNR row on the right are unchanged.
//
// Legacy METER_BAR_* constants are retained so a rollback to the flat bar
// needs nothing but un-calling drawNeedleGauge() and restoring the fillRect
// block. Delete them once the needle has been in master for a release.
constexpr int16_t METER_LABEL_X = 4;
constexpr int16_t METER_BAR_X   = 52;           // legacy — bar rollback only
constexpr int16_t METER_BAR_Y   = METER_Y + 6;  // legacy
constexpr int16_t METER_BAR_W   = 150;          // legacy
constexpr int16_t METER_BAR_H   = 14;           // legacy
constexpr int16_t METER_VAL_X   = METER_BAR_X + METER_BAR_W + 4;
constexpr int16_t METER_VAL_Y   = METER_BAR_Y + 2;  // nudge for FSSB9 baseline
constexpr int16_t METER_ROW2_Y  = METER_Y + 34;     // "SNR xx dB    stereo o"
constexpr int16_t STEREO_DOT_X  = 220;
constexpr int16_t STEREO_DOT_Y  = METER_ROW2_Y + 6;
constexpr int16_t STEREO_DOT_R  = 5;
constexpr uint8_t RSSI_SCALE_MAX_DBUV = 60;   // visible-scale upper bound

// --- Needle S-meter gauge --------------------------------------------------
// Sprite-backed analog gauge living in the left portion of the meter zone.
// Pivot is below the visible strip so only the top fan of a larger dial is
// drawn — classic vintage S-meter silhouette at 48 px tall.
//
// Geometry is in sprite-local coordinates; the sprite itself is pushed to
// (GAUGE_X, GAUGE_Y) on the TFT.
constexpr int16_t GAUGE_X      = 42;           // top-left X of sprite on TFT
constexpr int16_t GAUGE_Y      = METER_Y + 2;  // top-left Y of sprite on TFT
constexpr int16_t GAUGE_W      = 156;          // sprite width  (px)
constexpr int16_t GAUGE_H      = 44;           // sprite height (px)
constexpr int16_t GAUGE_PIVOT_X = GAUGE_W / 2; // needle pivot, sprite-local
constexpr int16_t GAUGE_PIVOT_Y = GAUGE_H + 14;// pivot below visible area
constexpr int16_t GAUGE_R_OUTER = 54;          // needle length (px)
constexpr int16_t GAUGE_R_INNER = 46;          // start of tick marks
constexpr int16_t GAUGE_R_TICK  = 52;          // end of tick marks
constexpr int16_t GAUGE_SWEEP_DEG = 60;        // half-sweep either side of up

constexpr uint16_t COL_GAUGE_BG    = TFT_BLACK;
constexpr uint16_t COL_GAUGE_ARC   = TFT_DARKGREY;
constexpr uint16_t COL_GAUGE_TICK  = TFT_WHITE;
constexpr uint16_t COL_GAUGE_LABEL = TFT_LIGHTGREY;
constexpr uint16_t COL_NEEDLE_LOW  = TFT_GREEN;
constexpr uint16_t COL_NEEDLE_MID  = TFT_YELLOW;
constexpr uint16_t COL_NEEDLE_HIGH = TFT_RED;

// --- Volume zone -----------------------------------------------------------
constexpr int16_t VOL_LABEL_X = 4;
constexpr int16_t VOL_LABEL_Y = VOL_Y + 12;
constexpr int16_t VOL_BAR_X   = 52;
constexpr int16_t VOL_BAR_Y   = VOL_Y + 14;
constexpr int16_t VOL_BAR_W   = 150;
constexpr int16_t VOL_BAR_H   = 18;
constexpr int16_t VOL_VAL_X   = VOL_BAR_X + VOL_BAR_W + 4;
constexpr int16_t VOL_VAL_Y   = VOL_BAR_Y - 2;

// --- Footer ----------------------------------------------------------------
constexpr int16_t FOOTER_TXT_X = 4;
constexpr int16_t FOOTER_TXT_Y = FOOTER_Y + 2;

// --- Touch zones (full-zone hit-boxes; freq top -> MODE_FREQUENCY, vol -> MODE_VOLUME)
constexpr int16_t TOUCH_FREQ_X = 0,         TOUCH_FREQ_Y = FREQ_Y;
constexpr int16_t TOUCH_FREQ_W = SCREEN_W,  TOUCH_FREQ_H = FREQ_H;
constexpr int16_t TOUCH_VOL_X  = 0,         TOUCH_VOL_Y  = VOL_Y;
constexpr int16_t TOUCH_VOL_W  = SCREEN_W,  TOUCH_VOL_H  = VOL_H;
constexpr unsigned long TOUCH_DEBOUNCE_MS = 200;

// --- Power source (knob — no battery hardware in v1, see future_improvements.md)
constexpr const char* POWER_SOURCE = "USB";

#endif  // UI_LAYOUT_H
