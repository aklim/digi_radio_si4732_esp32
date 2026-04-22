// ============================================================================
// Themes.h — colour-palette catalogue, ported 1:1 from ATS-Mini.
//
// Source: https://github.com/esp32-si4732/ats-mini/blob/main/ats-mini/Themes.h
// Every field and order matches the upstream struct so comparing visuals with
// the reference device is a direct byte-for-byte diff. Draw code references
// the active palette through the `TH` macro — e.g. `spr.setTextColor(TH.freq_text)`.
//
// Our fork-specific notes:
//   - The theme editor (switchThemeEditor) is not yet ported; its proto is
//     left declared so when we do port it later, callers need not change.
//   - ITEM_COUNT is a helper from Common.h in upstream; we provide a local
//     equivalent via sizeof.
// ============================================================================

#ifndef THEMES_H
#define THEMES_H

#include <stdint.h>

// Shorthand for the active theme, written so draw code reads identically to
// ATS-Mini: `TH.bg`, `TH.freq_text`, `TH.smeter_bar`, ...
#define TH (theme[themeIdx])

typedef struct __attribute__((packed)) {
    const char *name;
    uint16_t bg;
    uint16_t text;
    uint16_t text_muted;
    uint16_t text_warn;

    uint16_t smeter_icon;
    uint16_t smeter_bar;
    uint16_t smeter_bar_plus;
    uint16_t smeter_bar_empty;

    uint16_t save_icon;

    uint16_t stereo_icon;

    uint16_t rf_icon;
    uint16_t rf_icon_conn;

    uint16_t batt_voltage;
    uint16_t batt_border;
    uint16_t batt_full;
    uint16_t batt_low;
    uint16_t batt_charge;
    uint16_t batt_icon;

    uint16_t band_text;

    uint16_t mode_text;
    uint16_t mode_border;

    uint16_t box_bg;
    uint16_t box_border;
    uint16_t box_text;
    uint16_t box_off_bg;
    uint16_t box_off_text;

    uint16_t menu_bg;
    uint16_t menu_border;
    uint16_t menu_hdr;
    uint16_t menu_item;
    uint16_t menu_hl_bg;
    uint16_t menu_hl_text;
    uint16_t menu_param;

    uint16_t freq_text;
    uint16_t funit_text;
    uint16_t freq_hl;
    uint16_t freq_hl_sel;

    uint16_t rds_text;

    uint16_t scale_text;
    uint16_t scale_pointer;
    uint16_t scale_line;

    uint16_t scan_grid;
    uint16_t scan_snr;
    uint16_t scan_rssi;

    // Bottom touch-button row (Seek Down / Mute / Seek Up). Not part of the
    // ATS-Mini struct — added for the on-screen control surface that the
    // encoder-only reference device doesn't need. btn_bg should sit close
    // to bg so the row doesn't steal attention from the frequency / scale,
    // btn_active inverts fill + text for the latched Mute indicator.
    uint16_t btn_bg;
    uint16_t btn_fg;
    uint16_t btn_active;
} ColorTheme;

// Active-theme index (0..getTotalThemes()-1). Read/written from menu and
// persisted via persistSaveTheme()/persistLoadTheme().
extern uint8_t themeIdx;

// Theme catalogue — 9 presets in upstream order (Default / Bluesky / eInk /
// Pager / Orange / Night / Phosphor / Space / Magenta). Index stability is
// load-bearing: the persisted slot uses this index directly, so new themes
// must be *appended*, never inserted in the middle.
extern ColorTheme theme[];

// Count of entries in `theme[]`.
int getTotalThemes();

// Live-preview theme editor (menu overlay). Not yet ported; stub keeps the
// symbol available so draw code written 1:1 with upstream compiles.
bool switchThemeEditor(int8_t state = 2);

#endif  // THEMES_H
