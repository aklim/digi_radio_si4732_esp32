// ============================================================================
// Draw.h — layout offsets + draw-function prototypes, ported 1:1 from ATS-Mini.
//
// Source: https://github.com/esp32-si4732/ats-mini/blob/main/ats-mini/Draw.h
// Offset values copied verbatim so our sketch has the same widget geometry
// as the reference device. Our panel is 320×240 (vs ATS-Mini's 320×170);
// the extra 70 px land below the ATS-Mini "status row" (y>=135) and are
// claimed by the sidebar / scale / scan zones in later steps.
//
// Function-prototype subset: only the functions we port in this step are
// declared. Scale / sidebar / scan / about land in subsequent steps and
// their prototypes are added then.
// ============================================================================

#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

class TFT_eSPI;

// Register the TFT handle that every draw function will push pixels to.
// Must be called once in setup() before anything else in this header is
// invoked — otherwise draws no-op.
void drawInit(TFT_eSPI &tft);

// --- Display position control ---------------------------------------------
// (Verbatim from ats-mini/Draw.h so visuals match one-for-one.)
#define MENU_OFFSET_X         0
#define MENU_OFFSET_Y        18
#define ALT_MENU_OFFSET_X     0
#define ALT_MENU_OFFSET_Y     0
#define MENU_DELTA_X         10
#define METER_OFFSET_X        0
#define METER_OFFSET_Y        0
#define ALT_METER_OFFSET_X   75
#define ALT_METER_OFFSET_Y  136
#define SAVE_OFFSET_X        90
#define SAVE_OFFSET_Y         0
#define FREQ_OFFSET_X       250
#define FREQ_OFFSET_Y        62
#define FUNIT_OFFSET_X      255
#define FUNIT_OFFSET_Y       45
#define BAND_OFFSET_X       150
#define BAND_OFFSET_Y         9
#define ALT_STEREO_OFFSET_X 232
#define ALT_STEREO_OFFSET_Y  24
#define RDS_OFFSET_X        165
#define RDS_OFFSET_Y         94
#define STATUS_OFFSET_X     160
#define STATUS_OFFSET_Y     135
#define BATT_OFFSET_X       288
#define BATT_OFFSET_Y         0
#define WIFI_OFFSET_X       237
#define WIFI_OFFSET_Y         0
#define BLE_OFFSET_X        104
#define BLE_OFFSET_Y          0

// --- Battery (stub until real hardware lands) ------------------------------
// Returns true on battery / false on USB (i.e. "no voltage to display"). The
// stub currently always returns true with a fixed 4.15 V / 80 % SOC so the
// battery widget has something to render; the charge glyph path is unused.
bool drawBattery(int x, int y);

// --- Main widgets -----------------------------------------------------------
void drawBandAndMode(const char *band, const char *mode, int x, int y);
void drawFrequency(uint32_t freq, int x, int y, int ux, int uy);
void drawStationName(const char *name, int x, int y);
void drawSMeter(int strength, int x, int y);
void drawStereoIndicator(int x, int y, bool stereo);
void drawRadioText(int y, int ymax);
void drawScale(uint32_t freq);
void drawSideBar(int x, int y, int sx);

// --- Layout orchestrators ---------------------------------------------------
// Full-screen repaint (draws directly to the provided TFT, no sprite). Call
// from main.cpp whenever anything changed worth showing.
void drawLayoutDefault();

#endif  // DRAW_H
