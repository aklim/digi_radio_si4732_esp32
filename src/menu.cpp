// ============================================================================
// menu.cpp — full-screen modal menu for the TFT firmware.
//
// Navigation states:
//
//   MENU_TOP       top-level list (Band / BW / AGC / Theme / Scan / Close)
//       ├── click "Band"  → MENU_BAND
//       ├── click "BW"    → MENU_BW
//       ├── click "AGC"   → MENU_AGC
//       ├── click "Theme" → MENU_THEME
//       ├── click "Scan"  → start bandscope sweep + close menu
//       └── click "Close" → close menu
//
//   MENU_BAND      pick a band (one row per entry in radio.h g_bands[]
//                  + a trailing "Back" row).
//   MENU_BW        pick an IF filter from the active mode's catalogue
//                  (FM: 5 rows, AM/SW: 7 rows) + "Back".
//   MENU_AGC       pick AGC On / Off / Att NN from the active mode's
//                  table (FM: 28, AM: 38 rows) + "Back". List scrolls.
//   MENU_THEME     pick a palette from Themes.cpp catalogue + "Back".
//
// Rendering model: every time menuTakeDirty() returns true, main.cpp
// calls menuDraw(tft) which repaints the whole screen. The list drawer
// uses a viewport so long lists (e.g. 38-row AGC) stay inside the hint
// area instead of overrunning.
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
#include "connectivity.h"

namespace {

enum MenuState : uint8_t {
    MENU_STATE_CLOSED,
    MENU_STATE_TOP,
    MENU_STATE_BAND,
    MENU_STATE_BW,
    MENU_STATE_AGC,
    MENU_STATE_THEME,
    MENU_STATE_SETTINGS,
};

MenuState g_state   = MENU_STATE_CLOSED;
int       g_cursor  = 0;   // highlighted row in the current state
bool      g_dirty   = false;

// --- Top-level menu items ---------------------------------------------------
// Order here is the visible order. CMD_CLOSE stays last so "click-bottom
// = back out" muscle memory is consistent.
struct TopItem { const char* label; MenuCmd cmd; };
constexpr TopItem TOP_ITEMS[] = {
    { "Band",     CMD_BAND      },
    { "BW",       CMD_BANDWIDTH },
    { "AGC",      CMD_AGC       },
    { "Theme",    CMD_THEME     },
    { "Scan",     CMD_SCAN      },
    { "Settings", CMD_SETTINGS  },
    { "Close",    CMD_CLOSE     },
};
constexpr int TOP_COUNT = sizeof(TOP_ITEMS) / sizeof(TOP_ITEMS[0]);

// --- Band-picker items -----------------------------------------------------
// Count derived from the radio band table at render time so adding bands in
// radio.cpp automatically extends the menu. The implicit "Back" row sits
// at index == g_bandCount (rendered as a dim "Back" label).
int  bandItemCount()         { return (int)g_bandCount + 1; }
bool bandItemIsBack(int idx) { return idx >= (int)g_bandCount; }

// --- BW-picker items -------------------------------------------------------
// Count depends on the current band's mode (FM: 5, AM/SW: 7). Trailing
// "Back" row returns to the top menu.
int  bwItemCount()           { return (int)radioGetBandwidthCount() + 1; }
bool bwItemIsBack(int idx)   { return idx >= (int)radioGetBandwidthCount(); }

// --- AGC-picker items ------------------------------------------------------
// Count = radioGetAgcAttMax() + 1 (rows 0..max) + 1 back = max+2.
// FM max=27 -> 29 rows; AM max=37 -> 39 rows.
int  agcItemCount()          { return (int)radioGetAgcAttMax() + 2; }
bool agcItemIsBack(int idx)  { return idx >= (int)radioGetAgcAttMax() + 1; }

// --- Theme-picker items ----------------------------------------------------
// One row per entry in Themes.cpp + a trailing "Back" row.
int  themeItemCount()         { return getTotalThemes() + 1; }
bool themeItemIsBack(int idx) { return idx >= getTotalThemes(); }

// --- Settings submenu items ------------------------------------------------
// Boolean feature toggles — RDS / Bluetooth / WiFi. Order here drives the
// indices used by menuHandleClick below; keep the "Back" row last.
constexpr int SETTINGS_IDX_RDS  = 0;
constexpr int SETTINGS_IDX_BT   = 1;
constexpr int SETTINGS_IDX_WIFI = 2;
constexpr int SETTINGS_COUNT    = 4;     // 3 toggles + Back

bool settingsItemIsBack(int idx) { return idx >= SETTINGS_COUNT - 1; }

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
        case MENU_STATE_TOP:      return TOP_COUNT;
        case MENU_STATE_BAND:     return bandItemCount();
        case MENU_STATE_BW:       return bwItemCount();
        case MENU_STATE_AGC:      return agcItemCount();
        case MENU_STATE_THEME:    return themeItemCount();
        case MENU_STATE_SETTINGS: return SETTINGS_COUNT;
        default:                  return 0;
    }
}

// How many list rows can we render inside the MENU_LIST_Y..MENU_HINT_Y
// band without colliding with the hint? Used to clamp the scrolling
// viewport so long lists (e.g. 38-row AGC) stay inside the visible area.
int visibleRowCount() {
    int avail = MENU_HINT_Y - 8 - MENU_LIST_Y;
    int n     = avail / (MENU_ROW_H + 2);
    return n < 1 ? 1 : n;
}

// Compute the scroll offset so the current cursor is always visible. The
// scheme is "sticky top": the cursor rides the top edge when scrolling
// down, and rides the bottom edge when scrolling back up — standard
// list-view UX. Returns 0 when the whole list fits.
int viewportOffset(int cursor, int total) {
    int vis = visibleRowCount();
    if (total <= vis)             return 0;
    int maxOffset = total - vis;
    int off = cursor - vis + 1;
    if (off < 0)                  off = 0;
    if (off > maxOffset)          off = maxOffset;
    // Keep the cursor inside the window when navigating upward too.
    if (cursor < off)             off = cursor;
    return off;
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

// Generic list renderer. The caller supplies a label-provider callback
// (fills a short buffer with the label for a given index), a back-row
// predicate, the active-row index, and footer hint text. The renderer
// handles title bar, scrolling viewport, active-marker, and hint.
using LabelFn = void (*)(int idx, char *buf, size_t bufsize);

void drawList(TFT_eSPI& tft, const char *title, int total, int activeIdx,
              LabelFn label, bool (*isBack)(int), const char *hint) {
    drawTitle(tft, title);

    int offset  = viewportOffset(g_cursor, total);
    int visible = visibleRowCount();
    int y       = MENU_LIST_Y;

    for (int row = 0; row < visible && (offset + row) < total; row++) {
        int i = offset + row;
        bool highlighted = (i == g_cursor);
        if (isBack && isBack(i)) {
            drawRow(tft, y, "< Back", highlighted, /*dim=*/!highlighted);
        } else {
            char buf[24];
            label(i, buf, sizeof(buf));
            drawRow(tft, y, buf, highlighted);
            if (i == activeIdx) drawActiveMarker(tft, y, highlighted);
        }
        y += MENU_ROW_H + 2;
    }

    // Wipe any leftover pixels below the last rendered row.
    if (y < MENU_HINT_Y - 8) {
        tft.fillRect(0, y, SCREEN_W, MENU_HINT_Y - 8 - y, COL_BG);
    }
    drawHint(tft, hint);
}

// --- Label providers for each picker --------------------------------------

void labelTop(int idx, char *buf, size_t n) {
    snprintf(buf, n, "%s", TOP_ITEMS[idx].label);
}
void labelBand(int idx, char *buf, size_t n) {
    snprintf(buf, n, "%s", g_bands[idx].name);
}
void labelBw(int idx, char *buf, size_t n) {
    snprintf(buf, n, "%s", radioGetBandwidthDescAt((uint8_t)idx));
}
void labelAgc(int idx, char *buf, size_t n) {
    // Row 0 = AGC enabled; row 1 = AGC off no attenuation; row N = Att (N-1).
    if (idx == 0)      snprintf(buf, n, "AGC On");
    else if (idx == 1) snprintf(buf, n, "AGC Off");
    else               snprintf(buf, n, "Att %02d", idx - 1);
}
void labelTheme(int idx, char *buf, size_t n) {
    snprintf(buf, n, "%s", theme[idx].name);
}
void labelSettings(int idx, char *buf, size_t n) {
    // Row labels show the live state so the user can read off the
    // current value at a glance. Click toggles and the row repaints
    // because menuHandleClick sets g_dirty before returning.
    switch (idx) {
        case SETTINGS_IDX_RDS:
            snprintf(buf, n, "RDS: %s", radioGetRdsEnabled() ? "On" : "Off");
            break;
        case SETTINGS_IDX_BT:
            snprintf(buf, n, "Bluetooth: %s",
                     connectivityGetBtEnabled() ? "On" : "Off");
            break;
        case SETTINGS_IDX_WIFI:
            snprintf(buf, n, "WiFi: %s",
                     connectivityGetWifiEnabled() ? "On" : "Off");
            break;
        default:
            if (n) buf[0] = 0;
            break;
    }
}

// --- Per-state drawers (all delegate to drawList) -------------------------

void drawTopMenu(TFT_eSPI& tft) {
    drawList(tft, "Menu", TOP_COUNT, -1, labelTop, nullptr,
             "Rotate = select   Click = confirm");
}
void drawBandMenu(TFT_eSPI& tft) {
    drawList(tft, "Band", bandItemCount(), (int)radioGetBandIdx(),
             labelBand, bandItemIsBack, "* = current band");
}
void drawBwMenu(TFT_eSPI& tft) {
    drawList(tft, "BW", bwItemCount(), (int)radioGetBandwidthIdx(),
             labelBw, bwItemIsBack, "* = active filter");
}
void drawAgcMenu(TFT_eSPI& tft) {
    drawList(tft, "AGC", agcItemCount(), (int)radioGetAgcAttIdx(),
             labelAgc, agcItemIsBack, "* = active AGC");
}
void drawThemeMenu(TFT_eSPI& tft) {
    drawList(tft, "Theme", themeItemCount(), (int)themeIdx,
             labelTheme, themeItemIsBack, "* = active theme");
}
void drawSettingsMenu(TFT_eSPI& tft) {
    drawList(tft, "Settings", SETTINGS_COUNT, -1,
             labelSettings, settingsItemIsBack, "Click = toggle");
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
                // Start the sub-picker with the current value highlighted —
                // users invariably want to see where they are first.
                g_cursor = radioGetBandIdx();
                g_dirty  = true;
                return;
            case CMD_BANDWIDTH:
                transitionTo(MENU_STATE_BW);
                g_cursor = radioGetBandwidthIdx();
                g_dirty  = true;
                return;
            case CMD_AGC:
                transitionTo(MENU_STATE_AGC);
                g_cursor = radioGetAgcAttIdx();
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
            case CMD_SETTINGS:
                transitionTo(MENU_STATE_SETTINGS);
                return;
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

    if (g_state == MENU_STATE_BW) {
        if (bwItemIsBack(g_cursor)) {
            transitionTo(MENU_STATE_TOP);
            return;
        }
        uint8_t idx = (uint8_t)g_cursor;
        radioSetBandwidthIdx(idx);
        // Mode-aware persist: FM and AM keys are independent shadows in
        // persist.cpp so the non-active mode's saved filter survives.
        if (radioGetCurrentBand()->mode == MODE_FM) persistSaveBandwidthFm(idx);
        else                                       persistSaveBandwidthAm(idx);
        menuClose();
        return;
    }

    if (g_state == MENU_STATE_AGC) {
        if (agcItemIsBack(g_cursor)) {
            transitionTo(MENU_STATE_TOP);
            return;
        }
        uint8_t idx = (uint8_t)g_cursor;
        radioSetAgcAttIdx(idx);
        if (radioGetCurrentBand()->mode == MODE_FM) persistSaveAgcFm(idx);
        else                                       persistSaveAgcAm(idx);
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

    if (g_state == MENU_STATE_SETTINGS) {
        if (settingsItemIsBack(g_cursor)) {
            transitionTo(MENU_STATE_TOP);
            return;
        }
        // Toggle the matching flag, persist it, and stay inside the
        // Settings submenu so the user can flip several toggles in one
        // visit. g_dirty forces a repaint so the "On"/"Off" text flips
        // immediately.
        switch (g_cursor) {
            case SETTINGS_IDX_RDS: {
                bool en = !radioGetRdsEnabled();
                radioSetRdsEnabled(en);
                persistSaveRdsEnabled(en ? 1 : 0);
                break;
            }
            case SETTINGS_IDX_BT: {
                bool en = !connectivityGetBtEnabled();
                connectivitySetBtEnabled(en);
                persistSaveBtEnabled(en ? 1 : 0);
                break;
            }
            case SETTINGS_IDX_WIFI: {
                bool en = !connectivityGetWifiEnabled();
                connectivitySetWifiEnabled(en);
                persistSaveWifiEnabled(en ? 1 : 0);
                break;
            }
            default:
                break;
        }
        g_dirty = true;
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
        case MENU_STATE_TOP:      drawTopMenu(tft);      break;
        case MENU_STATE_BAND:     drawBandMenu(tft);     break;
        case MENU_STATE_BW:       drawBwMenu(tft);       break;
        case MENU_STATE_AGC:      drawAgcMenu(tft);      break;
        case MENU_STATE_THEME:    drawThemeMenu(tft);    break;
        case MENU_STATE_SETTINGS: drawSettingsMenu(tft); break;
        default: break;
    }
}
