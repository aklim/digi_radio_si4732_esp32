// ============================================================================
// menu.cpp — full-screen modal menu for the TFT firmware.
//
// Three navigation states at the moment:
//
//   MENU_TOP       top-level list (Band, Theme, Close)
//       ├── click "Band"  → MENU_BAND
//       ├── click "Theme" → MENU_THEME
//       └── click "Close" → close menu
//
//   MENU_BAND      pick a band (one row per entry in radio.h g_bands[]
//                  + a final "Back" row that returns to MENU_TOP without
//                  switching bands)
//       ├── click a band row → radioSetBand(idx) + close menu
//       └── click "Back"     → MENU_TOP
//
//   MENU_THEME     pick a palette (one row per entry in Themes.cpp
//                  catalogue + a trailing "Back" row)
//       ├── click a theme row → themeIdx = i + persist + close menu
//       └── click "Back"      → MENU_TOP
//
// Rendering model: every time menuTakeDirty() returns true, main.cpp
// calls menuDraw(tft) which repaints the whole screen. No per-row partial
// updates — the menu is modal, small, and infrequent, so keeping the
// drawing code dumb is worth the extra pixels pushed.
//
// Styling: reuses the GFX free-font pipeline enabled in PR #3. Header row
// in FreeSansBold12pt7b, list rows in FreeSansBold12pt7b (normal) or
// FreeSansBold12pt7b with inverted colours (highlight). Colours live in
// ui_layout.h.
// ============================================================================

#include "menu.h"

#include <TFT_eSPI.h>
// TFT_eSPI.h already pulls in every FreeXxxNpt7b GFXfont header (via
// LOAD_GFXFF -> Fonts/GFXFF/gfxfont.h). Those headers have no include
// guards, so re-including individual font files here would produce
// multi-definition errors. We just reference the struct names directly.

#include "radio.h"
#include "persist.h"
#include "input.h"
#include "ui_layout.h"
#include "Themes.h"
#include "Scan.h"

namespace {

enum MenuState : uint8_t {
    MENU_STATE_CLOSED,
    MENU_STATE_TOP,
    MENU_STATE_BAND,
    MENU_STATE_THEME,
};

MenuState g_state   = MENU_STATE_CLOSED;
int       g_cursor  = 0;   // highlighted row in the current state
bool      g_dirty   = false;

// --- Top-level menu items ---------------------------------------------------
// Order here is the visible order. When adding entries in follow-up PRs
// keep CMD_CLOSE as the last row so "click-bottom = back out" muscle memory
// is consistent.
struct TopItem { const char* label; MenuCmd cmd; };
constexpr TopItem TOP_ITEMS[] = {
    { "Band",  CMD_BAND  },
    { "Theme", CMD_THEME },
    { "Scan",  CMD_SCAN  },
    { "Close", CMD_CLOSE },
};
constexpr int TOP_COUNT = sizeof(TOP_ITEMS) / sizeof(TOP_ITEMS[0]);

// --- Band-picker items -----------------------------------------------------
// Count derived from the radio band table at render time so adding bands in
// radio.cpp automatically extends the menu. The implicit "Back" row sits
// at index == g_bandCount (rendered as a dim "Back" label).
int bandItemCount() { return (int)g_bandCount + 1; }
bool  bandItemIsBack(int idx) { return idx >= (int)g_bandCount; }

// --- Theme-picker items ----------------------------------------------------
// One row per entry in Themes.cpp + a trailing "Back" row.
int themeItemCount() { return getTotalThemes() + 1; }
bool themeItemIsBack(int idx) { return idx >= getTotalThemes(); }

// --- Layout constants (kept local since ui_layout.h is zone-focused) ---
constexpr int MENU_ROW_H      = 30;
constexpr int MENU_ROW_PAD_X  = 12;
constexpr int MENU_TITLE_Y    = 16;     // top padding from screen edge
constexpr int MENU_TITLE_H    = 36;
constexpr int MENU_LIST_Y     = MENU_TITLE_Y + MENU_TITLE_H + 4;
constexpr int MENU_HINT_Y     = SCREEN_H - 18;

// --- Helpers ---------------------------------------------------------------

int currentItemCount() {
    switch (g_state) {
        case MENU_STATE_TOP:   return TOP_COUNT;
        case MENU_STATE_BAND:  return bandItemCount();
        case MENU_STATE_THEME: return themeItemCount();
        default:               return 0;
    }
}

void clampCursor() {
    int n = currentItemCount();
    if (n <= 0) return;
    // Wrap so rotating past the ends jumps to the opposite end — ATS-Mini
    // does this and it's the standard rotary-encoder affordance.
    while (g_cursor < 0)   g_cursor += n;
    while (g_cursor >= n)  g_cursor -= n;
}

void transitionTo(MenuState s) {
    g_state  = s;
    g_cursor = 0;
    g_dirty  = true;
}

// Paint one list row. `inverted` draws the highlight bar.
void drawRow(TFT_eSPI& tft, int y, const char* label, bool inverted, bool dim = false) {
    uint16_t bg  = inverted ? COL_FOCUS       : COL_BG;
    uint16_t fg  = inverted ? COL_BG          : (dim ? COL_DIM_TXT : COL_LABEL_TXT);

    tft.fillRect(0, y, SCREEN_W, MENU_ROW_H, bg);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(ML_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.drawString(label, MENU_ROW_PAD_X, y + MENU_ROW_H / 2);
    tft.setTextDatum(TL_DATUM);
}

// Paint a sentinel marker to the right of the currently-active row (used in
// band picker to show which band is already selected — ATS-Mini uses a
// "*" for this).
void drawActiveMarker(TFT_eSPI& tft, int y, bool inverted) {
    uint16_t bg = inverted ? COL_FOCUS : COL_BG;
    uint16_t fg = inverted ? COL_BG    : COL_FREQ_TXT;
    tft.setTextColor(fg, bg);
    tft.setTextDatum(MR_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.drawString("*", SCREEN_W - MENU_ROW_PAD_X, y + MENU_ROW_H / 2);
    tft.setTextDatum(TL_DATUM);
}

void drawTitle(TFT_eSPI& tft, const char* title) {
    tft.fillRect(0, 0, SCREEN_W, MENU_LIST_Y - 4, COL_HEADER_BG);
    tft.setTextColor(COL_HEADER_TXT, COL_HEADER_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.drawString(title, SCREEN_W / 2, MENU_TITLE_Y + MENU_TITLE_H / 2);
    tft.setTextDatum(TL_DATUM);
}

void drawHint(TFT_eSPI& tft, const char* hint) {
    tft.fillRect(0, MENU_HINT_Y - 2, SCREEN_W, SCREEN_H - (MENU_HINT_Y - 2), COL_BG);
    tft.setTextColor(COL_DIM_TXT, COL_BG);
    tft.setTextDatum(BC_DATUM);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString(hint, SCREEN_W / 2, SCREEN_H - 4);
    tft.setTextDatum(TL_DATUM);
}

void drawTopMenu(TFT_eSPI& tft) {
    drawTitle(tft, "Menu");
    int y = MENU_LIST_Y;
    for (int i = 0; i < TOP_COUNT; i++) {
        drawRow(tft, y, TOP_ITEMS[i].label, i == g_cursor);
        y += MENU_ROW_H + 2;
    }
    // Fill the remaining list area with background so stale rows from a
    // previous state don't peek through. The menu repaints the full screen
    // anyway, but clearing the intermediate band explicitly documents that.
    if (y < MENU_HINT_Y - 8) {
        tft.fillRect(0, y, SCREEN_W, MENU_HINT_Y - 8 - y, COL_BG);
    }
    drawHint(tft, "Rotate = select   Click = confirm");
}

void drawBandMenu(TFT_eSPI& tft) {
    drawTitle(tft, "Band");
    int y       = MENU_LIST_Y;
    uint8_t cur = radioGetBandIdx();
    int n       = bandItemCount();

    for (int i = 0; i < n; i++) {
        bool highlighted = (i == g_cursor);
        if (bandItemIsBack(i)) {
            drawRow(tft, y, "< Back", highlighted, /*dim=*/!highlighted);
        } else {
            drawRow(tft, y, g_bands[i].name, highlighted);
            if (i == cur) drawActiveMarker(tft, y, highlighted);
        }
        y += MENU_ROW_H + 2;
    }
    if (y < MENU_HINT_Y - 8) {
        tft.fillRect(0, y, SCREEN_W, MENU_HINT_Y - 8 - y, COL_BG);
    }
    drawHint(tft, "* = current band");
}

void drawThemeMenu(TFT_eSPI& tft) {
    drawTitle(tft, "Theme");
    int y       = MENU_LIST_Y;
    uint8_t cur = themeIdx;
    int n       = themeItemCount();

    for (int i = 0; i < n; i++) {
        bool highlighted = (i == g_cursor);
        if (themeItemIsBack(i)) {
            drawRow(tft, y, "< Back", highlighted, /*dim=*/!highlighted);
        } else {
            drawRow(tft, y, theme[i].name, highlighted);
            if (i == cur) drawActiveMarker(tft, y, highlighted);
        }
        y += MENU_ROW_H + 2;
    }
    if (y < MENU_HINT_Y - 8) {
        tft.fillRect(0, y, SCREEN_W, MENU_HINT_Y - 8 - y, COL_BG);
    }
    drawHint(tft, "* = active theme");
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

bool menuIsOpen() { return g_state != MENU_STATE_CLOSED; }

void menuOpen() {
    transitionTo(MENU_STATE_TOP);
}

void menuClose() {
    g_state  = MENU_STATE_CLOSED;
    g_cursor = 0;
    // Intentionally leave g_dirty as-is — main.cpp doesn't consult
    // menuTakeDirty() when menuIsOpen() is false; it forces DIRTY_ALL on
    // close to repaint the main UI instead.
}

void menuHandleRotation(int delta) {
    if (delta == 0) return;
    g_cursor += delta;
    clampCursor();
    g_dirty = true;
}

void menuHandleClick() {
    if (g_state == MENU_STATE_TOP) {
        MenuCmd cmd = TOP_ITEMS[g_cursor].cmd;
        switch (cmd) {
            case CMD_BAND:
                transitionTo(MENU_STATE_BAND);
                // Start the band picker with the current band highlighted —
                // users invariably want to see where they are first.
                g_cursor = radioGetBandIdx();
                g_dirty  = true;
                return;
            case CMD_THEME:
                transitionTo(MENU_STATE_THEME);
                g_cursor = themeIdx;
                g_dirty  = true;
                return;
            case CMD_SCAN: {
                // Sweep SCAN_POINTS samples centred on the current tune.
                // Step granularity comes from the band's natural step.
                const Band *band = radioGetCurrentBand();
                scanStart(radioGetFrequency(), band->step);
                menuClose();
                return;
            }
            case CMD_CLOSE:
                menuClose();
                return;
            default:
                return;
        }
    }

    if (g_state == MENU_STATE_BAND) {
        if (bandItemIsBack(g_cursor)) {
            transitionTo(MENU_STATE_TOP);
            return;
        }
        uint8_t idx = (uint8_t)g_cursor;
        radioSetBand(idx);
        persistSaveBand(idx);
        // Persist the current-band-freq record too so reboots at the new
        // band land on the correct tune without waiting for the next
        // encoder rotation to trigger a save.
        persistSaveFrequency(idx, radioGetFrequency());
        menuClose();
        return;
    }

    if (g_state == MENU_STATE_THEME) {
        if (themeItemIsBack(g_cursor)) {
            transitionTo(MENU_STATE_TOP);
            return;
        }
        uint8_t idx = (uint8_t)g_cursor;
        themeIdx = idx;
        persistSaveTheme(idx);
        // menuClose() eventually triggers handleMenuClose() in main.cpp
        // which forces DIRTY_ALL — that repaint makes the new palette
        // visible in every zone that already consults TH.
        menuClose();
        return;
    }
}

bool menuTakeDirty() {
    bool d = g_dirty;
    g_dirty = false;
    return d;
}

void menuDraw(TFT_eSPI& tft) {
    // Belt-and-braces: always start with a black screen behind the menu so
    // any previous main-UI artifacts are wiped. The title / row repaint
    // overwrites parts of this but the bottom hint area relies on it.
    tft.fillScreen(COL_BG);

    switch (g_state) {
        case MENU_STATE_TOP:   drawTopMenu(tft);   break;
        case MENU_STATE_BAND:  drawBandMenu(tft);  break;
        case MENU_STATE_THEME: drawThemeMenu(tft); break;
        default: break;
    }
}
