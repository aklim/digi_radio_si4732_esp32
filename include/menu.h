// ============================================================================
// menu.h — full-screen modal menu for the TFT firmware.
//
// The menu is a takeover UI: while open it owns the entire screen and
// consumes all encoder input. main.cpp routes rotation + click events
// into the functions below when menuIsOpen() returns true; on close the
// main UI repaints from scratch (dirtyFlags = DIRTY_ALL).
//
// State is intentionally private to menu.cpp — this header only exposes the
// lifecycle operations and the "did anything change that needs a redraw"
// flag that drives the menu's paint loop.
//
// This is the skeleton that the rest of the ATS-Mini parity work will hang
// off of. Menu command codes follow ATS-Mini's 0x1000+ pattern
// (Menu.h :: CMD_BAND, CMD_VOLUME, etc.) so later PRs can slot CMD_STEP,
// CMD_MODE, CMD_MEMORY, etc. in without renaming the ones shipped here.
// ============================================================================

#ifndef MENU_H
#define MENU_H

#include <stdint.h>

class TFT_eSPI;  // forward-declared to keep the header light

// Command codes for menu items. The 16-bit width + 0x1000-prefixed grouping
// matches ats-mini/Menu.h so follow-up PRs can add entries from the
// ATS-Mini catalogue (CMD_VOLUME, CMD_AGC, CMD_BANDWIDTH, CMD_STEP, ...)
// without colliding with anything here.
// Slot assignments match ATS-Mini (ats-mini/Menu.h): CMD_AGC=0x1200 and
// CMD_BANDWIDTH=0x1300 are the upstream slots, CMD_SCAN lands at 0x1B00
// alongside the sweep / seek cluster. Our legacy CMD_SCAN=0x1200 moved
// to 0x1B00 in v2.2 — codes are internal enums and not persisted, so
// existing builds need no migration.
enum MenuCmd : uint16_t {
    CMD_NONE      = 0x0000,
    CMD_BAND      = 0x1000,   // pick an entry from radio.h g_bands[]
    CMD_AGC       = 0x1200,   // AGC on/off + manual attenuator
    CMD_BANDWIDTH = 0x1300,   // pick Si4732 IF filter for current mode
    CMD_THEME     = 0x2600,   // pick a palette from Themes.h catalogue
    CMD_SCAN      = 0x1B00,   // start a bandscope sweep around current freq
    CMD_MEMORY    = 0x2800,   // enter the memory-preset submenu (save/load stations)
    CMD_SETTINGS  = 0x2E00,   // enter the feature-toggle submenu
    CMD_ABOUT     = 0x2700,   // read-only firmware identity takeover (version + commit + date)
    CMD_CLOSE     = 0xFF00,   // dismiss the menu, return to main UI
};

// Is the menu currently taking over the screen? Main loop consults this to
// decide whether to route input to the menu or to the normal frequency /
// volume handlers.
bool menuIsOpen();

// Open / close the menu. menuOpen() resets the cursor to the first item so
// every long-press gives a predictable starting state.
void menuOpen();
void menuClose();

// Input handlers. Called by main.cpp while menuIsOpen() is true.
//   menuHandleRotation(delta) — delta > 0 moves the highlight down by delta
//                               items, delta < 0 moves it up. We receive a
//                               delta rather than an absolute encoder value
//                               because the encoder's raw count is meant for
//                               the frequency / volume scales, not menus.
//   menuHandleClick()         — confirm current selection. May close the
//                               menu (e.g. CMD_CLOSE) or descend into a
//                               sub-state (e.g. CMD_BAND → band picker).
void menuHandleRotation(int delta);
void menuHandleClick();

// Has the menu's internal state changed since the last paint? Clears the
// flag after reading — drive the paint loop with:
//     if (menuTakeDirty()) menuDraw(tft);
// Boot / open always paints by forcing the flag true.
bool menuTakeDirty();

// Paint the current menu state onto the display. Owns the entire screen
// (no coordination with the main UI's dirty-flag pipeline).
void menuDraw(TFT_eSPI& tft);

#endif  // MENU_H
