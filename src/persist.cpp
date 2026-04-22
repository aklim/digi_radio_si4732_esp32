// ============================================================================
// persist.cpp — NVS wrapper built on ESP32's <Preferences.h>.
//
// Rate-limiting strategy: every key gets a "dirty" flag + a last-write
// timestamp. Calls to persistSaveX() just update an in-RAM shadow and mark
// the key dirty. A single tick function (called from loop() via
// persistFlush() — or lazily on the next save that would otherwise flush
// immediately) commits dirty keys to NVS when >= PERSIST_MIN_WRITE_MS has
// elapsed since the last write for that key.
//
// For this PR we don't have a loop-driven flush; instead every save call
// invokes maybeFlush() which commits any pending write that has aged past
// the threshold. That means the last value of a fast-changing key (encoder
// spinning) might stay in RAM for up to PERSIST_MIN_WRITE_MS after the spin
// stops before landing on flash — acceptable for this project since power
// loss during tuning is unlikely and the band table is rebuilt from
// defaults on schema-version mismatch anyway.
//
// Namespace layout:
//   ver           u16   schema version (PERSIST_SCHEMA_VER)
//   band          u8    current band index
//   vol           u8    volume 0..MAX_VOLUME
//   freq<N>       u16   per-band last-tuned frequency (N = bandIdx, 0..15)
//   theme         u8    active UI theme index (v2+)
//   bw_fm         u8    FM IF-filter index (v3+, default 0 = Auto)
//   bw_am         u8    AM/SW IF-filter index (v3+, default 4 = 3.0k)
//   agc_fm        u8    FM AGC/attenuator index (v3+, 0 = AGC on, default 0)
//   agc_am        u8    AM/SW AGC/attenuator index (v3+, 0 = AGC on, default 0)
//   rds_en        u8    RDS decode enable (v4+, default 1)
//   bt_en         u8    Bluetooth enable (v4+, default 0)
//   wifi_en       u8    WiFi enable (v4+, default 0)
//   bl_level      u8    TFT backlight percent 0..100
//                       (v5+, default BACKLIGHT_DEFAULT_PERCENT)
//   preset<N>     u32   memory preset slot (N = 0..15; v6+, default 0 = empty;
//                       see preset_pack.h for the bit layout)
// ============================================================================

#include "persist.h"
#include "radio.h"       // g_bandCount, MAX_VOLUME
#include "backlight.h"   // BACKLIGHT_DEFAULT_PERCENT

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

namespace {

constexpr const char*  NS_NAME             = "radio";
constexpr unsigned long PERSIST_MIN_WRITE_MS = 1000;
constexpr uint8_t       MAX_BANDS_PERSISTED  = 16;   // freq0..freq15

Preferences g_prefs;
bool        g_opened = false;

// --- RAM shadows (current committed-to-RAM values) --------------------------
// v3 defaults: bw_am=4 matches radio.cpp g_bwIdxAm default ("3.0k"); all
// other new keys default to 0 (FM BW "Auto", AGC "On" for both modes).
uint8_t  g_bandIdx        = 0;
uint8_t  g_volume         = 0;
uint8_t  g_theme          = 0;
uint8_t  g_bwFm           = 0;
uint8_t  g_bwAm           = 4;
uint8_t  g_agcFm          = 0;
uint8_t  g_agcAm          = 0;
uint8_t  g_rdsEn          = 1;
uint8_t  g_btEn           = 0;
uint8_t  g_wifiEn         = 0;
uint8_t  g_blLevel        = BACKLIGHT_DEFAULT_PERCENT;
uint16_t g_freq[MAX_BANDS_PERSISTED] = {0};
uint32_t g_presets[PRESET_SLOT_COUNT] = {0};   // raw packed slot (see preset_pack.h)

// --- Dirty bookkeeping ------------------------------------------------------
bool          g_bandDirty  = false;
bool          g_volDirty   = false;
bool          g_themeDirty = false;
bool          g_bwFmDirty  = false;
bool          g_bwAmDirty  = false;
bool          g_agcFmDirty = false;
bool          g_agcAmDirty = false;
bool          g_rdsEnDirty  = false;
bool          g_btEnDirty   = false;
bool          g_wifiEnDirty = false;
bool          g_blLevelDirty = false;
bool          g_freqDirty[MAX_BANDS_PERSISTED] = {false};
bool          g_presetDirty[PRESET_SLOT_COUNT]  = {false};
unsigned long g_lastBandWrite  = 0;
unsigned long g_lastVolWrite   = 0;
unsigned long g_lastThemeWrite = 0;
unsigned long g_lastBwFmWrite  = 0;
unsigned long g_lastBwAmWrite  = 0;
unsigned long g_lastAgcFmWrite = 0;
unsigned long g_lastAgcAmWrite = 0;
unsigned long g_lastRdsEnWrite  = 0;
unsigned long g_lastBtEnWrite   = 0;
unsigned long g_lastWifiEnWrite = 0;
unsigned long g_lastBlLevelWrite = 0;
unsigned long g_lastFreqWrite[MAX_BANDS_PERSISTED] = {0};
unsigned long g_lastPresetWrite[PRESET_SLOT_COUNT]  = {0};

bool ensureOpen() {
    if (g_opened) return true;
    // Second arg false = read-write. Namespace is auto-created on first use.
    g_opened = g_prefs.begin(NS_NAME, false);
    if (!g_opened) {
        Serial.println(F("[persist] Preferences.begin() failed"));
    }
    return g_opened;
}

void commitBand() {
    if (!g_bandDirty || !ensureOpen()) return;
    g_prefs.putUChar("band", g_bandIdx);
    g_bandDirty     = false;
    g_lastBandWrite = millis();
}

void commitVolume() {
    if (!g_volDirty || !ensureOpen()) return;
    g_prefs.putUChar("vol", g_volume);
    g_volDirty     = false;
    g_lastVolWrite = millis();
}

void commitTheme() {
    if (!g_themeDirty || !ensureOpen()) return;
    g_prefs.putUChar("theme", g_theme);
    g_themeDirty     = false;
    g_lastThemeWrite = millis();
}

void commitBwFm() {
    if (!g_bwFmDirty || !ensureOpen()) return;
    g_prefs.putUChar("bw_fm", g_bwFm);
    g_bwFmDirty     = false;
    g_lastBwFmWrite = millis();
}

void commitBwAm() {
    if (!g_bwAmDirty || !ensureOpen()) return;
    g_prefs.putUChar("bw_am", g_bwAm);
    g_bwAmDirty     = false;
    g_lastBwAmWrite = millis();
}

void commitAgcFm() {
    if (!g_agcFmDirty || !ensureOpen()) return;
    g_prefs.putUChar("agc_fm", g_agcFm);
    g_agcFmDirty     = false;
    g_lastAgcFmWrite = millis();
}

void commitAgcAm() {
    if (!g_agcAmDirty || !ensureOpen()) return;
    g_prefs.putUChar("agc_am", g_agcAm);
    g_agcAmDirty     = false;
    g_lastAgcAmWrite = millis();
}

void commitRdsEn() {
    if (!g_rdsEnDirty || !ensureOpen()) return;
    g_prefs.putUChar("rds_en", g_rdsEn);
    g_rdsEnDirty     = false;
    g_lastRdsEnWrite = millis();
}

void commitBtEn() {
    if (!g_btEnDirty || !ensureOpen()) return;
    g_prefs.putUChar("bt_en", g_btEn);
    g_btEnDirty     = false;
    g_lastBtEnWrite = millis();
}

void commitWifiEn() {
    if (!g_wifiEnDirty || !ensureOpen()) return;
    g_prefs.putUChar("wifi_en", g_wifiEn);
    g_wifiEnDirty     = false;
    g_lastWifiEnWrite = millis();
}

void commitBlLevel() {
    if (!g_blLevelDirty || !ensureOpen()) return;
    g_prefs.putUChar("bl_level", g_blLevel);
    g_blLevelDirty     = false;
    g_lastBlLevelWrite = millis();
}

void commitFrequency(uint8_t idx) {
    if (idx >= MAX_BANDS_PERSISTED) return;
    if (!g_freqDirty[idx] || !ensureOpen()) return;
    char key[8];
    snprintf(key, sizeof(key), "freq%u", (unsigned)idx);
    g_prefs.putUShort(key, g_freq[idx]);
    g_freqDirty[idx]     = false;
    g_lastFreqWrite[idx] = millis();
}

void commitPreset(uint8_t idx) {
    if (idx >= PRESET_SLOT_COUNT) return;
    if (!g_presetDirty[idx] || !ensureOpen()) return;
    char key[12];
    snprintf(key, sizeof(key), "preset%u", (unsigned)idx);
    g_prefs.putULong(key, g_presets[idx]);
    g_presetDirty[idx]     = false;
    g_lastPresetWrite[idx] = millis();
}

// Commit any key whose dirty-age has reached the rate-limit threshold.
void maybeFlushExpired() {
    unsigned long now = millis();
    if (g_bandDirty  && (now - g_lastBandWrite  >= PERSIST_MIN_WRITE_MS)) commitBand();
    if (g_volDirty   && (now - g_lastVolWrite   >= PERSIST_MIN_WRITE_MS)) commitVolume();
    if (g_themeDirty && (now - g_lastThemeWrite >= PERSIST_MIN_WRITE_MS)) commitTheme();
    if (g_bwFmDirty  && (now - g_lastBwFmWrite  >= PERSIST_MIN_WRITE_MS)) commitBwFm();
    if (g_bwAmDirty  && (now - g_lastBwAmWrite  >= PERSIST_MIN_WRITE_MS)) commitBwAm();
    if (g_agcFmDirty && (now - g_lastAgcFmWrite >= PERSIST_MIN_WRITE_MS)) commitAgcFm();
    if (g_agcAmDirty && (now - g_lastAgcAmWrite >= PERSIST_MIN_WRITE_MS)) commitAgcAm();
    if (g_rdsEnDirty  && (now - g_lastRdsEnWrite  >= PERSIST_MIN_WRITE_MS)) commitRdsEn();
    if (g_btEnDirty   && (now - g_lastBtEnWrite   >= PERSIST_MIN_WRITE_MS)) commitBtEn();
    if (g_wifiEnDirty && (now - g_lastWifiEnWrite >= PERSIST_MIN_WRITE_MS)) commitWifiEn();
    if (g_blLevelDirty && (now - g_lastBlLevelWrite >= PERSIST_MIN_WRITE_MS)) commitBlLevel();
    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        if (g_freqDirty[i] && (now - g_lastFreqWrite[i] >= PERSIST_MIN_WRITE_MS)) {
            commitFrequency(i);
        }
    }
    for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
        if (g_presetDirty[i] && (now - g_lastPresetWrite[i] >= PERSIST_MIN_WRITE_MS)) {
            commitPreset(i);
        }
    }
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

void persistInit() {
    if (!ensureOpen()) return;

    uint16_t ver = g_prefs.getUShort("ver", 0);
    if (ver == 0) {
        // First boot — stamp the current version so later branches treat
        // the namespace as already-current.
        Serial.println(F("[persist] first boot; initialising namespace"));
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 1 && PERSIST_SCHEMA_VER == 6) {
        // v1 -> v6 additive: seed theme plus all v3 BW/AGC keys plus the
        // v4 feature-enable flags plus the v5 backlight level plus the v6
        // memory-preset slots. Band / vol / freq survive as-is.
        Serial.println(F("[persist] upgrading schema v1 -> v6"));
        g_prefs.putUChar("theme",    0);
        g_prefs.putUChar("bw_fm",    0);
        g_prefs.putUChar("bw_am",    4);
        g_prefs.putUChar("agc_fm",   0);
        g_prefs.putUChar("agc_am",   0);
        g_prefs.putUChar("rds_en",   1);
        g_prefs.putUChar("bt_en",    0);
        g_prefs.putUChar("wifi_en",  0);
        g_prefs.putUChar("bl_level", BACKLIGHT_DEFAULT_PERCENT);
        for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
            char key[12]; snprintf(key, sizeof(key), "preset%u", (unsigned)i);
            g_prefs.putULong(key, 0u);
        }
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 2 && PERSIST_SCHEMA_VER == 6) {
        // v2 -> v6 adds the BW/AGC per-mode indices, the v4 flags, the
        // v5 backlight level, and the v6 memory-preset slots.
        Serial.println(F("[persist] upgrading schema v2 -> v6"));
        g_prefs.putUChar("bw_fm",    0);
        g_prefs.putUChar("bw_am",    4);
        g_prefs.putUChar("agc_fm",   0);
        g_prefs.putUChar("agc_am",   0);
        g_prefs.putUChar("rds_en",   1);
        g_prefs.putUChar("bt_en",    0);
        g_prefs.putUChar("wifi_en",  0);
        g_prefs.putUChar("bl_level", BACKLIGHT_DEFAULT_PERCENT);
        for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
            char key[12]; snprintf(key, sizeof(key), "preset%u", (unsigned)i);
            g_prefs.putULong(key, 0u);
        }
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 3 && PERSIST_SCHEMA_VER == 6) {
        // v3 -> v6 adds the feature-enable flags, the backlight level, and
        // the memory-preset slots.
        Serial.println(F("[persist] upgrading schema v3 -> v6"));
        g_prefs.putUChar("rds_en",   1);
        g_prefs.putUChar("bt_en",    0);
        g_prefs.putUChar("wifi_en",  0);
        g_prefs.putUChar("bl_level", BACKLIGHT_DEFAULT_PERCENT);
        for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
            char key[12]; snprintf(key, sizeof(key), "preset%u", (unsigned)i);
            g_prefs.putULong(key, 0u);
        }
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 4 && PERSIST_SCHEMA_VER == 6) {
        // v4 -> v6 adds the backlight level and the memory-preset slots.
        // Backlight default matches the v5 migration: v4 units booted at a
        // hard-coded 86% duty (~220/255) and the power-reduction PR dropped
        // the default to BACKLIGHT_DEFAULT_PERCENT, so upgraders land on
        // the new lower default immediately.
        Serial.println(F("[persist] upgrading schema v4 -> v6"));
        g_prefs.putUChar("bl_level", BACKLIGHT_DEFAULT_PERCENT);
        for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
            char key[12]; snprintf(key, sizeof(key), "preset%u", (unsigned)i);
            g_prefs.putULong(key, 0u);
        }
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 5 && PERSIST_SCHEMA_VER == 6) {
        // v5 -> v6 adds only the 16 memory-preset slots. All other keys
        // from v5 (band / vol / freq / theme / BW / AGC / feature flags /
        // bl_level) survive untouched.
        Serial.println(F("[persist] upgrading schema v5 -> v6"));
        for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
            char key[12]; snprintf(key, sizeof(key), "preset%u", (unsigned)i);
            g_prefs.putULong(key, 0u);
        }
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver != PERSIST_SCHEMA_VER) {
        Serial.print(F("[persist] schema mismatch (stored="));
        Serial.print(ver);
        Serial.print(F(" current="));
        Serial.print(PERSIST_SCHEMA_VER);
        Serial.println(F("). Wiping namespace."));
        g_prefs.clear();
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    }

    g_bandIdx = g_prefs.getUChar("band", 0);
    if (g_bandIdx >= g_bandCount) g_bandIdx = 0;   // guard against stale index

    g_volume = g_prefs.getUChar("vol", 0);
    if (g_volume > MAX_VOLUME) g_volume = 0;

    g_theme = g_prefs.getUChar("theme", 0);
    // Themes.cpp clamps out-of-range indices on its side, so no bounds
    // check is needed here.

    // v3 BW/AGC shadows. Defaults match radio.cpp's kBwFm[0] / kBwAm[4] /
    // AGC-on so a freshly-wiped namespace (or a v1 upgrade that fails to
    // seed for any reason) still lands on sane values.
    g_bwFm  = g_prefs.getUChar("bw_fm",  0);
    g_bwAm  = g_prefs.getUChar("bw_am",  4);
    g_agcFm = g_prefs.getUChar("agc_fm", 0);
    g_agcAm = g_prefs.getUChar("agc_am", 0);

    // v4 feature flags. Defaults match the migration-path seeds so a
    // partially-initialised namespace still lands on sane values.
    g_rdsEn  = g_prefs.getUChar("rds_en",  1);
    g_btEn   = g_prefs.getUChar("bt_en",   0);
    g_wifiEn = g_prefs.getUChar("wifi_en", 0);

    // v5 backlight level. Clamped to 0..100 on load — the stored value
    // always comes from persistSaveBacklight() which clamps, but guarding
    // here too means a corrupted NVS entry can't drive ledcWrite() with a
    // garbage duty (the backlight module clamps internally too).
    g_blLevel = g_prefs.getUChar("bl_level", BACKLIGHT_DEFAULT_PERCENT);
    if (g_blLevel > 100) g_blLevel = BACKLIGHT_DEFAULT_PERCENT;

    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        char key[8];
        snprintf(key, sizeof(key), "freq%u", (unsigned)i);
        g_freq[i] = g_prefs.getUShort(key, 0);
    }

    // v6 memory presets. Missing keys (e.g. a freshly-wiped namespace before
    // the migration branches above have seeded them, or a hand-rolled partial
    // state) decode to 0 which presetUnpack() renders as an empty slot, so
    // the UI shows "<empty>" rather than garbage.
    for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
        char key[12];
        snprintf(key, sizeof(key), "preset%u", (unsigned)i);
        g_presets[i] = g_prefs.getULong(key, 0u);
    }
}

void persistFlush() {
    if (g_bandDirty)  commitBand();
    if (g_volDirty)   commitVolume();
    if (g_themeDirty) commitTheme();
    if (g_bwFmDirty)  commitBwFm();
    if (g_bwAmDirty)  commitBwAm();
    if (g_agcFmDirty) commitAgcFm();
    if (g_agcAmDirty) commitAgcAm();
    if (g_rdsEnDirty)  commitRdsEn();
    if (g_btEnDirty)   commitBtEn();
    if (g_wifiEnDirty) commitWifiEn();
    if (g_blLevelDirty) commitBlLevel();
    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        if (g_freqDirty[i]) commitFrequency(i);
    }
    for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
        if (g_presetDirty[i]) commitPreset(i);
    }
}

uint8_t persistLoadBand() { return g_bandIdx; }

void persistSaveBand(uint8_t idx) {
    if (idx == g_bandIdx) return;
    g_bandIdx   = idx;
    g_bandDirty = true;
    // Band changes are rare and high-value — flush immediately instead of
    // waiting out the rate-limit window. Frequency / volume writes are the
    // high-frequency ones we need to coalesce.
    commitBand();
    maybeFlushExpired();
}

uint16_t persistLoadFrequency(uint8_t bandIdx) {
    if (bandIdx >= MAX_BANDS_PERSISTED) return 0;
    return g_freq[bandIdx];
}

void persistSaveFrequency(uint8_t bandIdx, uint16_t freq) {
    if (bandIdx >= MAX_BANDS_PERSISTED) return;
    if (g_freq[bandIdx] == freq) return;
    g_freq[bandIdx]     = freq;
    g_freqDirty[bandIdx] = true;
    maybeFlushExpired();
}

uint8_t persistLoadVolume() { return g_volume; }

void persistSaveVolume(uint8_t vol) {
    if (vol == g_volume) return;
    g_volume   = vol;
    g_volDirty = true;
    maybeFlushExpired();
}

uint8_t persistLoadTheme() { return g_theme; }

void persistSaveTheme(uint8_t idx) {
    if (idx == g_theme) return;
    g_theme      = idx;
    g_themeDirty = true;
    // Theme changes are low-frequency menu actions — flush immediately,
    // matching persistSaveBand()'s pattern.
    commitTheme();
    maybeFlushExpired();
}

// BW and AGC are likewise low-frequency (single menu click per change) so
// flush immediately. The RAM shadow is updated unconditionally so re-selecting
// the same value still refreshes the last-write timestamp cleanly.

uint8_t persistLoadBandwidthFm() { return g_bwFm; }

void persistSaveBandwidthFm(uint8_t idx) {
    if (idx == g_bwFm) return;
    g_bwFm      = idx;
    g_bwFmDirty = true;
    commitBwFm();
    maybeFlushExpired();
}

uint8_t persistLoadBandwidthAm() { return g_bwAm; }

void persistSaveBandwidthAm(uint8_t idx) {
    if (idx == g_bwAm) return;
    g_bwAm      = idx;
    g_bwAmDirty = true;
    commitBwAm();
    maybeFlushExpired();
}

uint8_t persistLoadAgcFm() { return g_agcFm; }

void persistSaveAgcFm(uint8_t idx) {
    if (idx == g_agcFm) return;
    g_agcFm      = idx;
    g_agcFmDirty = true;
    commitAgcFm();
    maybeFlushExpired();
}

uint8_t persistLoadAgcAm() { return g_agcAm; }

void persistSaveAgcAm(uint8_t idx) {
    if (idx == g_agcAm) return;
    g_agcAm      = idx;
    g_agcAmDirty = true;
    commitAgcAm();
    maybeFlushExpired();
}

// Feature-enable flags. Menu toggles are low-frequency (one click per change)
// so commit immediately — matches the theme/band pattern above.

uint8_t persistLoadRdsEnabled() { return g_rdsEn; }

void persistSaveRdsEnabled(uint8_t en) {
    uint8_t v = en ? 1 : 0;
    if (v == g_rdsEn) return;
    g_rdsEn      = v;
    g_rdsEnDirty = true;
    commitRdsEn();
    maybeFlushExpired();
}

uint8_t persistLoadBtEnabled() { return g_btEn; }

void persistSaveBtEnabled(uint8_t en) {
    uint8_t v = en ? 1 : 0;
    if (v == g_btEn) return;
    g_btEn      = v;
    g_btEnDirty = true;
    commitBtEn();
    maybeFlushExpired();
}

uint8_t persistLoadWifiEnabled() { return g_wifiEn; }

void persistSaveWifiEnabled(uint8_t en) {
    uint8_t v = en ? 1 : 0;
    if (v == g_wifiEn) return;
    g_wifiEn      = v;
    g_wifiEnDirty = true;
    commitWifiEn();
    maybeFlushExpired();
}

uint8_t persistLoadBacklight() { return g_blLevel; }

void persistSaveBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    if (percent == g_blLevel) return;
    g_blLevel      = percent;
    g_blLevelDirty = true;
    // Brightness changes are low-frequency menu actions (one click per
    // change) — flush immediately, matching the theme/toggle pattern.
    commitBlLevel();
    maybeFlushExpired();
}

// --- Memory presets (v6) ---------------------------------------------------
// Save / clear are menu-driven so flush immediately like theme/band; no
// rate-limit coalescing is needed for this family. Load is a pure RAM
// read — the raw u32 is decoded through preset_pack.h.

PresetSlot persistLoadPreset(uint8_t slot) {
    if (slot >= PRESET_SLOT_COUNT) return PresetSlot{0, 0, 0};
    return presetUnpack(g_presets[slot]);
}

void persistSavePreset(uint8_t slot, PresetSlot p) {
    if (slot >= PRESET_SLOT_COUNT) return;
    uint32_t raw = presetPack(p);
    if (raw == g_presets[slot]) return;
    g_presets[slot]      = raw;
    g_presetDirty[slot]  = true;
    commitPreset(slot);
    maybeFlushExpired();
}

void persistClearPreset(uint8_t slot) {
    if (slot >= PRESET_SLOT_COUNT) return;
    if (g_presets[slot] == 0u) return;
    g_presets[slot]     = 0u;
    g_presetDirty[slot] = true;
    commitPreset(slot);
    maybeFlushExpired();
}

bool persistPresetIsValid(uint8_t slot) {
    if (slot >= PRESET_SLOT_COUNT) return false;
    return (g_presets[slot] & PRESET_VALID_BIT) != 0u;
}

uint16_t persistFindPresetFreq(uint8_t band, uint16_t currentFreq, int dir) {
    // Single pass over all 16 slots. Track the best "strictly past current"
    // freq in the requested direction, plus a wrap fallback (the opposite
    // extreme across all matching presets). Decouples from slot order —
    // the user model is "next higher station", not "next saved slot".
    uint16_t bestForward  = 0;
    bool     foundForward = false;
    uint16_t wrapFallback = 0;
    bool     foundAny     = false;
    bool up = dir > 0;

    for (uint8_t i = 0; i < PRESET_SLOT_COUNT; i++) {
        PresetSlot p = persistLoadPreset(i);
        if (!p.valid || p.band != band) continue;
        foundAny = true;

        if (up) {
            if (p.freq > currentFreq) {
                if (!foundForward || p.freq < bestForward) {
                    bestForward  = p.freq;
                    foundForward = true;
                }
            }
            // For wrap, we want the lowest overall.
            if (wrapFallback == 0 || p.freq < wrapFallback) wrapFallback = p.freq;
        } else {
            if (p.freq < currentFreq) {
                if (!foundForward || p.freq > bestForward) {
                    bestForward  = p.freq;
                    foundForward = true;
                }
            }
            // For wrap, we want the highest overall.
            if (p.freq > wrapFallback) wrapFallback = p.freq;
        }
    }

    if (!foundAny) return 0;
    return foundForward ? bestForward : wrapFallback;
}
