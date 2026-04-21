// ============================================================================
// backlight.h — TFT backlight brightness control (LEDC PWM on TFT_BL).
//
// Separated from main.cpp so the menu layer can apply brightness changes
// without main.cpp having to expose internal LEDC channel state. The level
// is expressed in whole percent (0..100) for a clean user-facing API; the
// module internally maps percent → 0..255 8-bit LEDC duty.
//
// The persisted brightness level lives in persist.cpp (key "bl_level",
// schema v5+). main.cpp applies the restored level right after persistInit().
// ============================================================================

#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <stdint.h>

// Default brightness percent used on first boot (fresh NVS) and as the seed
// value for every schema migration path. Chosen as a compromise between
// visibility in a lit room and power draw — the TFT's backlight LED is the
// single largest continuous power consumer on the board after the Si4732.
constexpr uint8_t BACKLIGHT_DEFAULT_PERCENT = 55;

// Allowed brightness levels offered by the Settings → Brightness picker.
// Kept coarse on purpose: five steps are enough to find a comfortable
// level, and the discrete set simplifies persistence (no need to round).
constexpr uint8_t BACKLIGHT_LEVELS[]   = {20, 40, 60, 80, 100};
constexpr int     BACKLIGHT_LEVEL_COUNT = sizeof(BACKLIGHT_LEVELS) / sizeof(BACKLIGHT_LEVELS[0]);

// Configure the LEDC channel on TFT_BL (5 kHz, 8-bit resolution) and drive
// the pin at the BACKLIGHT_DEFAULT_PERCENT duty. Call before any display
// activity that relies on the panel being lit (drawSplash etc.). Idempotent
// on repeat calls but not expected to be called more than once.
void backlightInit();

// Drive the LEDC pin at `percent` duty (clamped to 0..100). Updates the
// in-RAM shadow consulted by backlightGet(); does NOT persist — callers that
// want the change to survive reboot should also call persistSaveBacklight()
// with the same percent.
void backlightApply(uint8_t percent);

// Last value passed to backlightApply() (or BACKLIGHT_DEFAULT_PERCENT if no
// call has been made yet). Used by the menu to highlight the currently
// active level in the Brightness picker.
uint8_t backlightGet();

#endif  // BACKLIGHT_H
