// ============================================================================
// ui_layout_tft.h — UI coordinates, sizes, colors, and fonts for the TFT
// (Waveshare 2.8" ST7789V, 240x320 portrait, rotation 0).
//
// Keeping magic numbers out of main_tft.cpp means docs/display_tft.md can
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

#ifndef UI_LAYOUT_TFT_H
#define UI_LAYOUT_TFT_H

#include <stdint.h>
#include <TFT_eSPI.h>   // for TFT_* color macros

// --- Panel -----------------------------------------------------------------
constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;

// --- Physical orientation --------------------------------------------------
// When DISPLAY_FLIPPED is true the UI is drawn upside-down (portrait 180),
// useful when the shield is mounted in a case that cannot be rotated. Set
// to false when the shield is mounted right-side-up.
//
// Touch consequence: the hard-coded XPT2046 calibration in main_tft.cpp was
// captured with the panel in rotation 0, and TFT_eSPI's getTouch() does NOT
// rotate coordinates on its own — it just applies the stored calibration.
// main_tft.cpp's handleTouch() therefore manually mirrors the touch point
// (x -> W-1-x, y -> H-1-y) when this flag is true so finger taps still land
// on the visually-correct zone. If taps ever feel inverted, toggle this.
constexpr bool DISPLAY_FLIPPED = true;

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
// FONT7 = 7-segment style, numeric + '.' only — perfect for the frequency.
// FONT4 = 26 px sans — labels, volume number, SNR number.
// FONT2 = 16 px sans — footer, RDS RadioText body.
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
constexpr int16_t METER_LABEL_X = 4;
constexpr int16_t METER_BAR_X   = 52;
constexpr int16_t METER_BAR_Y   = METER_Y + 6;
constexpr int16_t METER_BAR_W   = 150;
constexpr int16_t METER_BAR_H   = 14;
constexpr int16_t METER_VAL_X   = METER_BAR_X + METER_BAR_W + 4;
constexpr int16_t METER_VAL_Y   = METER_BAR_Y - 1;
constexpr int16_t METER_ROW2_Y  = METER_Y + 28;     // "SNR xx dB    stereo o"
constexpr int16_t STEREO_DOT_X  = 220;
constexpr int16_t STEREO_DOT_Y  = METER_ROW2_Y + 6;
constexpr int16_t STEREO_DOT_R  = 5;
constexpr uint8_t RSSI_SCALE_MAX_DBUV = 60;   // visible-bar range

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

#endif  // UI_LAYOUT_TFT_H
