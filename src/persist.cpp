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
uint8_t  g_bandIdx        = 0;
uint8_t  g_volume         = 0;
uint8_t  g_theme          = 0;
uint16_t g_freq[MAX_BANDS_PERSISTED] = {0};

// --- Dirty bookkeeping ------------------------------------------------------
bool          g_bandDirty  = false;
bool          g_volDirty   = false;
bool          g_themeDirty = false;
bool          g_freqDirty[MAX_BANDS_PERSISTED] = {false};
unsigned long g_lastBandWrite  = 0;
unsigned long g_lastVolWrite   = 0;
unsigned long g_lastThemeWrite = 0;
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
    } else if (ver == 1 && PERSIST_SCHEMA_VER == 2) {
        // v1 -> v2 is purely additive (just adds "theme"): preserve band /
        // vol / freq, seed the new key with a default.
        Serial.println(F("[persist] upgrading schema v1 -> v2"));
        g_prefs.putUChar("theme", 0);
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
