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
// ============================================================================

#include "persist.h"
#include "radio.h"   // g_bandCount, MAX_VOLUME

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
uint16_t g_freq[MAX_BANDS_PERSISTED] = {0};

// --- Dirty bookkeeping ------------------------------------------------------
bool          g_bandDirty  = false;
bool          g_volDirty   = false;
bool          g_themeDirty = false;
bool          g_bwFmDirty  = false;
bool          g_bwAmDirty  = false;
bool          g_agcFmDirty = false;
bool          g_agcAmDirty = false;
bool          g_freqDirty[MAX_BANDS_PERSISTED] = {false};
unsigned long g_lastBandWrite  = 0;
unsigned long g_lastVolWrite   = 0;
unsigned long g_lastThemeWrite = 0;
unsigned long g_lastBwFmWrite  = 0;
unsigned long g_lastBwAmWrite  = 0;
unsigned long g_lastAgcFmWrite = 0;
unsigned long g_lastAgcAmWrite = 0;
unsigned long g_lastFreqWrite[MAX_BANDS_PERSISTED] = {0};

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

void commitFrequency(uint8_t idx) {
    if (idx >= MAX_BANDS_PERSISTED) return;
    if (!g_freqDirty[idx] || !ensureOpen()) return;
    char key[8];
    snprintf(key, sizeof(key), "freq%u", (unsigned)idx);
    g_prefs.putUShort(key, g_freq[idx]);
    g_freqDirty[idx]     = false;
    g_lastFreqWrite[idx] = millis();
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
    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        if (g_freqDirty[i] && (now - g_lastFreqWrite[i] >= PERSIST_MIN_WRITE_MS)) {
            commitFrequency(i);
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
    } else if (ver == 1 && PERSIST_SCHEMA_VER == 3) {
        // v1 -> v3 additive: seed theme (from the old v1->v2 path) plus the
        // four new BW/AGC keys. Band / vol / freq survive as-is.
        Serial.println(F("[persist] upgrading schema v1 -> v3"));
        g_prefs.putUChar("theme",  0);
        g_prefs.putUChar("bw_fm",  0);
        g_prefs.putUChar("bw_am",  4);
        g_prefs.putUChar("agc_fm", 0);
        g_prefs.putUChar("agc_am", 0);
        g_prefs.putUShort("ver", PERSIST_SCHEMA_VER);
    } else if (ver == 2 && PERSIST_SCHEMA_VER == 3) {
        // v2 -> v3 adds the BW/AGC per-mode indices. Everything else stays.
        Serial.println(F("[persist] upgrading schema v2 -> v3"));
        g_prefs.putUChar("bw_fm",  0);
        g_prefs.putUChar("bw_am",  4);
        g_prefs.putUChar("agc_fm", 0);
        g_prefs.putUChar("agc_am", 0);
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

    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        char key[8];
        snprintf(key, sizeof(key), "freq%u", (unsigned)i);
        g_freq[i] = g_prefs.getUShort(key, 0);
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
    for (uint8_t i = 0; i < MAX_BANDS_PERSISTED; i++) {
        if (g_freqDirty[i]) commitFrequency(i);
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
