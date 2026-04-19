// ============================================================================
// radio.cpp — Si4732 multi-band receiver wrapper (FM + MW/SW AM) with a
//             dedicated FreeRTOS task pinned to Core 0 driving the I2C
//             polling on its own cadence.
//
// The Si4732 RESET pin is NOT connected to any GPIO. An external RC circuit
// handles hardware reset at power-on, so the PU2CLR SI4735 library's normal
// setup() -> reset() flow is bypassed — we drive setPowerUp() + radioPowerUp()
// directly over I2C, matching the sequence that has been running reliably on
// the firmware since v1.0.0.
//
// Band layout ported from ATS-Mini's Common.h + Menu.cpp (see
// https://github.com/esp32-si4732/ats-mini). We start with four bands (FM
// Broadcast, MW, SW 41m, SW 31m); LW and additional SW segments plus SSB
// are tracked separately in docs/future_improvements.md and will land in
// follow-up PRs. Index 0 is FM Broadcast so first-boot state (before NVS
// has a saved band) is identical to the v1 single-band behaviour.
//
// RDS mirror buffers: the library exposes getRdsText0A / getRdsText2A as
// char* into shared buffers that can change under us mid-read. We keep
// local mirrors (g_ps, g_rt) — writes from the task are single-threaded,
// reads from the UI take the mutex.
//
// Threading (see radio.h for the full contract):
//   * g_mutex serialises every access to the Si4735 library instance and
//     to the cached-state globals (g_rssi, g_snr, g_stereo, g_ps, g_rt,
//     g_signalChanged, g_rdsChanged, g_bandIdx, per-band currentFreq).
//   * radioTaskBody (pinned to Core 0) is the only place that issues the
//     periodic signal / RDS polls; the UI on Core 1 consumes cached state.
//   * radioSetXxx functions (called from UI) take the same mutex around
//     the library call — band switches briefly block the task on the
//     order of one task-tick.
// ============================================================================

#include "radio.h"
#include "radio_format.h"
#include "rds_sanitize.h"

#include <Arduino.h>
#include <SI4735.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

// Band table definitions live in band_table.cpp so they can be linked into
// native host unit tests without pulling in this file's Arduino / SI4735 /
// FreeRTOS dependencies. See radio_bands.h for the declarations.

// --- Owned state (private to this translation unit) -------------------------
static SI4735  g_radio;

static uint8_t g_bandIdx = 0;
static uint8_t g_volume  = DEFAULT_VOLUME;
static uint8_t g_rssi    = 0;
static uint8_t g_snr     = 0;
static bool    g_stereo  = false;

// Change flags drained by radioPollSignal() / radioPollRds(). The task
// sets them whenever a cached value actually moved; the UI clears them on
// read. Using plain bools under the mutex instead of atomics keeps the
// reasoning simple: one writer (task), one reader (UI), both gated.
static bool    g_signalChanged = false;
static bool    g_rdsChanged    = false;

// --- Signal-quality polling -------------------------------------------------
constexpr unsigned long SIGNAL_POLL_INTERVAL_MS = 500;
static unsigned long g_lastSignalPoll = 0;

// --- RDS polling ------------------------------------------------------------
constexpr unsigned long RDS_POLL_INTERVAL_MS = 200;
constexpr unsigned long RDS_STALE_MS         = 10000;  // clear text after 10s no sync
static unsigned long g_lastRdsPoll = 0;
static unsigned long g_lastRdsSync = 0;

// Local mirrors of the RDS Programme Service (8 chars) and RadioText (64 chars)
// plus terminator. Zero-initialised => empty string => "no RDS".
static char     g_ps[9]  = {0};
static char     g_rt[65] = {0};
static uint16_t g_rdsPi  = 0;  // 16-bit PI code; 0 == "not decoded"

// --- Bandwidth catalogues (verbatim indices/labels from ats-mini) ----------
// FM filter: 5 presets; SI4735 setFmBandwidth takes the `filter_idx` field.
struct BwEntry { uint8_t filter; const char *desc; };
static const BwEntry kBwFm[] = {
    { 0, "Auto" },   // automatic (default)
    { 1, "110k" },
    { 2, "84k"  },
    { 3, "60k"  },
    { 4, "40k"  },
};
// AM/SW/MW filter: 7 presets; SI4735 setBandwidth takes AMCHFLT, AMPLFLT.
static const BwEntry kBwAm[] = {
    { 4, "1.0k" },
    { 5, "1.8k" },
    { 3, "2.0k" },
    { 6, "2.5k" },
    { 2, "3.0k" },
    { 1, "4.0k" },
    { 0, "6.0k" },
};
static const uint8_t kBwFmCount = sizeof(kBwFm) / sizeof(kBwFm[0]);
static const uint8_t kBwAmCount = sizeof(kBwAm) / sizeof(kBwAm[0]);
// Default picks match upstream's defaultBwIdx[] (FM=0 "Auto", AM=4 "3.0k").
static uint8_t g_bwIdxFm = 0;
static uint8_t g_bwIdxAm = 4;

// --- AGC + manual attenuator (packed into SI4735's AGCDIS / AGCIDX) --------
// Per-mode shadows, matching ATS-Mini's doAgc() (Menu.cpp:754-773):
//   idx == 0      -> AGCDIS=0 (enabled), AGCIDX ignored
//   idx == 1      -> AGCDIS=1 (disabled), AGCIDX = 0 (no attenuation)
//   idx == 2..N   -> AGCDIS=1 (disabled), AGCIDX = idx - 1
// FM range: 0..kAgcIdxMaxFm (28 entries); AM range: 0..kAgcIdxMaxAm (38).
// FM and AM are tracked independently so switching bands between modes
// doesn't clobber the other mode's setting.
constexpr uint8_t kAgcIdxMaxFm = 27;
constexpr uint8_t kAgcIdxMaxAm = 37;
static uint8_t g_agcIdxFm = 0;
static uint8_t g_agcIdxAm = 0;

// --- Bandscope sweep gate ---------------------------------------------------
// When Scan.cpp owns the chip it bumps this flag. The Core-0 poll loop
// bails early so it doesn't fight the sweep for I2C cycles; cached
// RSSI / RDS values freeze until the scan releases them.
static volatile bool g_scanActive = false;

// --- FreeRTOS primitives ----------------------------------------------------
// g_mutex is created in radioInit() *before* any other thread can observe
// the module. radioStart() creates the task after radioInit() returns, so
// the task never sees a null mutex.
static SemaphoreHandle_t g_mutex      = nullptr;
static TaskHandle_t       g_taskHandle = nullptr;

constexpr uint32_t RADIO_TASK_STACK    = 4096;
constexpr UBaseType_t RADIO_TASK_PRIO  = 1;
constexpr BaseType_t  RADIO_TASK_CORE  = 0;    // pin to pro_cpu; loopTask lives on app_cpu (1)
constexpr TickType_t  RADIO_TASK_TICK  = pdMS_TO_TICKS(20);

// ============================================================================
// Internal helpers — called with g_mutex already held.
// ============================================================================

static Band& activeBandLocked() { return g_bands[g_bandIdx]; }

// Re-apply the active mode's saved BW / AGC indices to the chip. Pulled out
// of the public setters so applyBandLocked() can call them after setFM /
// setAM without re-entering the mutex. Caller holds g_mutex.
static void applyBwLocked() {
    BandMode mode = activeBandLocked().mode;
    if (mode == MODE_FM) {
        uint8_t idx = g_bwIdxFm;
        if (idx >= kBwFmCount) idx = 0;
        g_radio.setFmBandwidth(kBwFm[idx].filter);
    } else {
        uint8_t idx = g_bwIdxAm;
        if (idx >= kBwAmCount) idx = 4;  // 3.0 kHz default
        // AMPLFLT=1 enables the power-line noise filter (ATS-Mini default).
        g_radio.setBandwidth(kBwAm[idx].filter, 1);
    }
}

static void applyAgcLocked() {
    BandMode mode = activeBandLocked().mode;
    uint8_t idx   = (mode == MODE_FM) ? g_agcIdxFm : g_agcIdxAm;
    uint8_t maxIdx = (mode == MODE_FM) ? kAgcIdxMaxFm : kAgcIdxMaxAm;
    if (idx > maxIdx) idx = 0;
    if (idx == 0) {
        // AGC enabled, attenuator ignored.
        g_radio.setAutomaticGainControl(0, 0);
    } else {
        // AGC disabled, AGCIDX = idx - 1 (per ATS-Mini's doAgc convention).
        g_radio.setAutomaticGainControl(1, idx - 1);
    }
}

// Apply the current band's settings to the Si4735. Caller holds g_mutex.
static void applyBandLocked() {
    Band& b = activeBandLocked();

    if (b.mode == MODE_FM) {
        g_radio.setFM(b.minFreq, b.maxFreq, b.currentFreq, b.step);
        // Permissive RDS block-error thresholds — more data at the cost of
        // occasional garbled characters, which is the right tradeoff for a
        // portable FM receiver.
        g_radio.setRdsConfig(1, 3, 3, 3, 3);
    } else {
        // AM / MW / SW. SSB would take a separate setSSB path once the lib
        // patch loader lands in a later PR.
        g_radio.setAM(b.minFreq, b.maxFreq, b.currentFreq, b.step);
    }

    // Volume survives band switches. setFM/setAM in the PU2CLR lib leaves
    // volume untouched at the chip level, but re-applying costs one I2C
    // write and eliminates any doubt about post-switch state.
    g_radio.setVolume(g_volume);

    // Re-apply the active mode's saved BW + AGC so user selections survive
    // band switches. Without this, setFM/setAM reverts the chip to each
    // mode's power-on default on every band switch (FM: Auto, AM: 2 kHz).
    applyBwLocked();
    applyAgcLocked();
}

// Poll signal quality on the task's cadence. Returns true when any cached
// value changed; caller (the task) OR-sets g_signalChanged accordingly.
// Caller holds g_mutex.
static bool pollSignalLocked() {
    // Bandscope sweep owns the chip — stay out of its way until it
    // finishes. Cached RSSI / SNR freeze for the duration, which is
    // acceptable (UI shows the scan graph, not the live meter).
    if (g_scanActive) return false;
    unsigned long now = millis();
    if (now - g_lastSignalPoll < SIGNAL_POLL_INTERVAL_MS) return false;
    g_lastSignalPoll = now;

    g_radio.getCurrentReceivedSignalQuality();
    uint8_t rssi   = g_radio.getCurrentRSSI();
    uint8_t snr    = g_radio.getCurrentSNR();
    bool    stereo = (activeBandLocked().mode == MODE_FM) && g_radio.getCurrentPilot();

    bool changed = (rssi != g_rssi) || (snr != g_snr) || (stereo != g_stereo);
    g_rssi   = rssi;
    g_snr    = snr;
    g_stereo = stereo;
    return changed;
}

// RadioText sanitiser lives in rds_sanitize.cpp (see rds_sanitize.h) so the
// logic can be exercised by native host unit tests. Used below when pulling
// 2A group text out of the PU2CLR library.

// Poll RDS on the task's cadence. No-op on non-FM bands. Caller holds mutex.
static bool pollRdsLocked() {
    if (g_scanActive) return false;
    if (activeBandLocked().mode != MODE_FM) return false;

    unsigned long now = millis();
    if (now - g_lastRdsPoll < RDS_POLL_INTERVAL_MS) return false;
    g_lastRdsPoll = now;

    bool changed = false;

    // INTACK=0, MTFIFO=0, STATUSONLY=0 — normal status read with ack of queued data.
    g_radio.getRdsStatus();

    // Triple-gate matching ATS-Mini (Station.cpp:233): Received + Sync +
    // SyncFound. Without SyncFound the library sometimes hands back
    // partially-aligned garbage that looks receive-worthy but isn't.
    if (g_radio.getRdsSync() && g_radio.getRdsSyncFound()) {
        g_lastRdsSync = now;

        if (g_radio.getRdsReceived()) {
            char* libPs = g_radio.getRdsStationName();  // 0A group -> PS
            char* libRt = g_radio.getRdsText2A();       // 2A group -> RadioText

            if (libPs && strncmp(g_ps, libPs, 8) != 0) {
                // PS is short (8 chars) and changes rarely enough that a
                // direct copy is fine; the UI has its own scroll guard.
                strncpy(g_ps, libPs, 8);
                g_ps[8] = 0;
                changed = true;
            }

            // RT goes through the sanitiser. We only publish when the
            // sanitised buffer is non-empty and actually differs from the
            // previous published value — empty / all-control-char inputs
            // leave g_rt untouched so the UI keeps showing the last good
            // text (or falls back to the band scale if g_rt was already
            // empty).
            //
            // cleaned[] MUST be fully zero-initialised before sanitize so
            // that every byte past the written terminator is also NUL.
            // drawRadioText uses a multi-line walk `rt += strlen(rt)+1`
            // expecting double-NUL termination (ATS-Mini convention); if
            // we memcpy 65 bytes with stack garbage after the first NUL,
            // the UI renders random characters where the band scale
            // should be.
            if (libRt) {
                char cleaned[65] = {0};
                if (rdsSanitizeRt(libRt, cleaned, sizeof(cleaned))) {
                    if (strncmp(g_rt, cleaned, 64) != 0) {
                        memcpy(g_rt, cleaned, 65);
                        changed = true;
                    }
                }
            }

            // PI is part of every RDS block and usually arrives within the
            // first packet after sync. Store it into the mirror unchanged
            // so the sidebar shows a stable 4-hex-digit station ID.
            uint16_t pi = g_radio.getRdsPI();
            if (pi && pi != g_rdsPi) {
                g_rdsPi = pi;
                changed = true;
            }
        }
    } else if (g_lastRdsSync && (now - g_lastRdsSync > RDS_STALE_MS)) {
        // Lost sync long enough that any displayed text is almost certainly
        // from a different station. Clear so the UI shows "no RDS" instead of
        // a stale PS name.
        if (g_ps[0] || g_rt[0] || g_rdsPi) {
            g_ps[0] = 0;
            g_rt[0] = 0;
            g_rdsPi = 0;
            changed = true;
        }
    }

    return changed;
}

// ============================================================================
// Radio task — pinned to Core 0, wakes every RADIO_TASK_TICK ms, polls the
// chip if the per-kind rate-limit window has elapsed, and flags state
// changes for the UI to consume.
// ============================================================================

static void radioTaskBody(void* /*arg*/) {
    Serial.printf("[radio] task running on core %d\n", xPortGetCoreID());
    for (;;) {
        if (xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE) {
            bool sc = pollSignalLocked();
            bool rc = pollRdsLocked();
            if (sc) g_signalChanged = true;
            if (rc) g_rdsChanged    = true;
            xSemaphoreGive(g_mutex);
        }
        vTaskDelay(RADIO_TASK_TICK);
    }
}

// ============================================================================
// Public API
// ============================================================================

void radioInit() {
    Serial.println(F("[radio] initializing Si4732 (no reset pin, RC-driven)..."));

    // Create the mutex before any cross-core visibility of this module's
    // state. Until radioStart() fires there is only one caller (setup on
    // Core 1), but we take/release the mutex anyway so the same code path
    // works before and after the task exists.
    g_mutex = xSemaphoreCreateMutex();
    configASSERT(g_mutex != nullptr);

    // SEN pin wired to GND -> I2C address 0x11 (library index 0).
    g_radio.setDeviceI2CAddress(0);

    // CTSIEN=0, GPO2OEN=0, PATCH=0, XOSCEN=1, FUNC=FM (we'll retune to the
    // current band right after), OPMODE=analog audio.
    g_radio.setPowerUp(0, 0, 0, XOSCEN_CRYSTAL, POWER_UP_FM, SI473X_ANALOG_AUDIO);
    g_radio.radioPowerUp();

    // Crystal oscillator stabilisation.
    delay(250);

    g_radio.setVolume(g_volume);
    g_radio.getFirmware();

    applyBandLocked();

    Band& b = activeBandLocked();
    b.currentFreq = g_radio.getFrequency();
    g_volume      = g_radio.getVolume();

    Serial.print(F("[radio] Si4732 ready. Firmware PN: "));
    Serial.println(g_radio.getFirmwarePN());
    Serial.print(F("[radio] Band: "));
    Serial.print(b.name);
    Serial.print(F("  Tuned to: "));
    Serial.println(b.currentFreq);
}

void radioStart() {
    if (g_taskHandle) return;   // idempotent — protect against double-call
    BaseType_t ok = xTaskCreatePinnedToCore(
        radioTaskBody, "radio",
        RADIO_TASK_STACK, nullptr,
        RADIO_TASK_PRIO, &g_taskHandle,
        RADIO_TASK_CORE);
    if (ok != pdPASS) {
        // Catastrophic but extremely unlikely — log loudly. The UI will
        // still render but signal / RDS polling stops; radioSetXxx stays
        // functional via the mutex on Core 1.
        Serial.println(F("[radio] ERROR: xTaskCreatePinnedToCore failed"));
        g_taskHandle = nullptr;
    }
}

void radioSetBand(uint8_t idx) {
    if (idx >= g_bandCount) return;

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (idx != g_bandIdx) {
        // Snapshot the outgoing band's tune so we can restore on return.
        activeBandLocked().currentFreq = g_radio.getFrequency();

        g_bandIdx = idx;
        applyBandLocked();

        // Fresh, empty signal cache — the UI redraws from "no data"
        // rather than showing the previous band's last reading until the
        // next 500 ms poll fires.
        g_rssi   = 0;
        g_snr    = 0;
        g_stereo = false;
        g_lastSignalPoll = 0;
        g_signalChanged  = true;

        // RDS is meaningless off-FM; clear mirrors so UI peeks see empty.
        g_ps[0] = 0;
        g_rt[0] = 0;
        g_rdsPi = 0;
        g_lastRdsSync = 0;
        g_rdsChanged  = true;

        Band& b = activeBandLocked();
        Serial.print(F("[radio] Band switch -> "));
        Serial.print(b.name);
        Serial.print(F("  freq="));
        Serial.println(b.currentFreq);
    }
    xSemaphoreGive(g_mutex);
}

uint8_t radioGetBandIdx() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = g_bandIdx;
    xSemaphoreGive(g_mutex);
    return v;
}

const Band* radioGetCurrentBand() {
    // Return a stable pointer. Individual fields may mutate; callers that
    // need a consistent snapshot should call radioGetFrequency() (mutex-
    // protected) rather than dereferencing ->currentFreq.
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const Band* p = &g_bands[g_bandIdx];
    xSemaphoreGive(g_mutex);
    return p;
}

void radioSetFrequency(uint16_t freq) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    Band& b = activeBandLocked();
    if (freq < b.minFreq) freq = b.minFreq;
    if (freq > b.maxFreq) freq = b.maxFreq;

    g_radio.setFrequency(freq);
    b.currentFreq = freq;

    // Drop any text from the previous station. See same rationale as v1:
    // we only copy from the library's buffers when getRdsSync() is true,
    // and sync is lost on tune, so the library's stale bytes won't leak
    // into our mirrors before the new station locks in.
    if (b.mode == MODE_FM) {
        g_ps[0] = 0;
        g_rt[0] = 0;
        g_rdsPi = 0;
        g_lastRdsSync = 0;
        g_rdsChanged  = true;
    }
    xSemaphoreGive(g_mutex);
}

uint16_t radioGetFrequency() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint16_t v = activeBandLocked().currentFreq;
    xSemaphoreGive(g_mutex);
    return v;
}

void radioSetVolume(uint8_t v) {
    if (v > MAX_VOLUME) v = MAX_VOLUME;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_radio.setVolume(v);
    g_volume = v;
    xSemaphoreGive(g_mutex);
}

uint8_t radioGetVolume() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = g_volume;
    xSemaphoreGive(g_mutex);
    return v;
}

void radioFormatFrequency(char* buf, size_t bufsize) {
    // Snapshot the two fields we need under the mutex so the task can't
    // swap the band out from under us between reading mode and freq.
    // The actual string build is pure and lives in radio_format.h so the
    // formatter can be unit-tested natively.
    BandMode mode;
    uint16_t freq;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    mode = activeBandLocked().mode;
    freq = activeBandLocked().currentFreq;
    xSemaphoreGive(g_mutex);

    radioFormatFrequencyPure(mode, freq, buf, bufsize);
}

bool radioPollSignal() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    bool changed = g_signalChanged;
    g_signalChanged = false;
    xSemaphoreGive(g_mutex);
    return changed;
}

uint8_t radioGetRssi() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = g_rssi;
    xSemaphoreGive(g_mutex);
    return v;
}

uint8_t radioGetSnr() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = g_snr;
    xSemaphoreGive(g_mutex);
    return v;
}

bool radioIsStereo() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    bool v = g_stereo;
    xSemaphoreGive(g_mutex);
    return v;
}

bool radioPollRds() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    bool changed = g_rdsChanged;
    g_rdsChanged = false;
    xSemaphoreGive(g_mutex);
    return changed;
}

void radioGetRdsPs(char* buf, size_t bufsize) {
    if (!buf || bufsize < 1) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    size_t n = bufsize - 1 < sizeof(g_ps) - 1 ? bufsize - 1 : sizeof(g_ps) - 1;
    memcpy(buf, g_ps, n);
    buf[n] = 0;
    xSemaphoreGive(g_mutex);
}

void radioGetRdsRt(char* buf, size_t bufsize) {
    if (!buf || bufsize < 1) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    size_t n = bufsize - 1 < sizeof(g_rt) - 1 ? bufsize - 1 : sizeof(g_rt) - 1;
    memcpy(buf, g_rt, n);
    buf[n] = 0;
    xSemaphoreGive(g_mutex);
}

uint16_t radioGetRdsPi() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint16_t pi = g_rdsPi;
    xSemaphoreGive(g_mutex);
    return pi;
}

// --- Bandwidth --------------------------------------------------------------
// The catalogue used at call time depends on the active band's mode.
// Callers that index into the "current" table get consistent indices as
// long as they stay on one mode; after a band switch, the default index
// for the new mode applies (0 for FM, 4 for AM).

static const BwEntry* currentBwTable(BandMode mode, uint8_t &count, uint8_t &activeIdx) {
    if (mode == MODE_FM) {
        count     = kBwFmCount;
        activeIdx = g_bwIdxFm;
        return kBwFm;
    } else {
        count     = kBwAmCount;
        activeIdx = g_bwIdxAm;
        return kBwAm;
    }
}

uint8_t radioGetBandwidthIdx() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t count, idx;
    (void)currentBwTable(activeBandLocked().mode, count, idx);
    xSemaphoreGive(g_mutex);
    return idx;
}

uint8_t radioGetBandwidthCount() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t count, idx;
    (void)currentBwTable(activeBandLocked().mode, count, idx);
    xSemaphoreGive(g_mutex);
    return count;
}

const char* radioGetBandwidthDesc() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t count, idx;
    const BwEntry *tbl = currentBwTable(activeBandLocked().mode, count, idx);
    const char *desc = tbl[idx].desc;
    xSemaphoreGive(g_mutex);
    return desc;
}

const char* radioGetBandwidthDescAt(uint8_t idx) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t count, _idx;
    const BwEntry *tbl = currentBwTable(activeBandLocked().mode, count, _idx);
    const char *desc = (idx < count) ? tbl[idx].desc : "--";
    xSemaphoreGive(g_mutex);
    return desc;
}

void radioSetBandwidthIdx(uint8_t idx) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    BandMode mode = activeBandLocked().mode;
    uint8_t count, _idx;
    (void)currentBwTable(mode, count, _idx);
    if (idx >= count) idx = count - 1;

    // Stash the new index in the mode-specific slot so it survives a
    // band switch that doesn't change mode (FM -> FM, AM -> AM ...).
    if (mode == MODE_FM) g_bwIdxFm = idx;
    else                 g_bwIdxAm = idx;
    applyBwLocked();
    xSemaphoreGive(g_mutex);
}

// Seed the BW shadow for one mode without touching the chip. Used at boot
// (main.cpp setup() before radioInit() returns) to load NVS values into
// radio.cpp's mutable state; the subsequent applyBandLocked() inside
// radioInit() picks them up on the first setFM / setAM sequence.
//
// Safe to call before the radio task starts (mutex already exists after
// radioInit() creates it; before radioInit() the caller must not race). No
// I2C traffic happens here so the order relative to chip power-up is free.
void radioSeedBandwidthIdx(bool fm, uint8_t idx) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (fm) {
        if (idx >= kBwFmCount) idx = 0;
        g_bwIdxFm = idx;
    } else {
        if (idx >= kBwAmCount) idx = 4;
        g_bwIdxAm = idx;
    }
    xSemaphoreGive(g_mutex);
}

// --- AGC + attenuator -------------------------------------------------------
// Per-mode indices to match ATS-Mini (FmAgcIdx 0..27, AmAgcIdx 0..37).
// `radioGetAgcAttIdx` / `radioSetAgcAttIdx` operate on the currently-active
// mode's slot; see header for the index semantics.

uint8_t radioGetAgcAttIdx() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = (activeBandLocked().mode == MODE_FM) ? g_agcIdxFm : g_agcIdxAm;
    xSemaphoreGive(g_mutex);
    return v;
}

uint8_t radioGetAgcAttMax() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    uint8_t v = (activeBandLocked().mode == MODE_FM) ? kAgcIdxMaxFm : kAgcIdxMaxAm;
    xSemaphoreGive(g_mutex);
    return v;
}

bool radioAgcIsOn() {
    return radioGetAgcAttIdx() == 0;
}

void radioSetAgcAttIdx(uint8_t idx) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    BandMode mode = activeBandLocked().mode;
    uint8_t maxIdx = (mode == MODE_FM) ? kAgcIdxMaxFm : kAgcIdxMaxAm;
    if (idx > maxIdx) idx = maxIdx;
    if (mode == MODE_FM) g_agcIdxFm = idx;
    else                 g_agcIdxAm = idx;
    applyAgcLocked();
    xSemaphoreGive(g_mutex);
}

// Seed the AGC shadow for one mode without touching the chip; see
// radioSeedBandwidthIdx() above for the intended usage pattern.
void radioSeedAgcIdx(bool fm, uint8_t idx) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (fm) {
        if (idx > kAgcIdxMaxFm) idx = 0;
        g_agcIdxFm = idx;
    } else {
        if (idx > kAgcIdxMaxAm) idx = 0;
        g_agcIdxAm = idx;
    }
    xSemaphoreGive(g_mutex);
}

void radioApplyCurrentBand() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    applyBandLocked();
    xSemaphoreGive(g_mutex);
}

// --- Bandscope sweep hooks --------------------------------------------------

void radioScanEnter() {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_scanActive = true;
    // Mute audio so the listener does not hear the freq sweep and its
    // squeals / unmodulated noise bursts. Unmute in radioScanExit().
    g_radio.setAudioMute(true);
    xSemaphoreGive(g_mutex);
}

void radioScanExit(uint16_t restoreFreq) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    // Restore the listener's original tune and un-mute. Cached RSSI /
    // SNR will refresh on the next regular pollSignalLocked() call.
    g_radio.setFrequency(restoreFreq);
    activeBandLocked().currentFreq = restoreFreq;
    g_radio.setAudioMute(false);
    g_scanActive     = false;
    g_lastSignalPoll = 0;  // force a fresh poll on the next tick
    g_signalChanged  = true;
    xSemaphoreGive(g_mutex);
}

void radioScanMeasure(uint16_t freq, uint16_t settleMs,
                      uint8_t &outRssi, uint8_t &outSnr) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_radio.setFrequency(freq);
    // Unlock while the chip settles so the menu / ui can still peek;
    // we re-lock to read the quality registers.
    xSemaphoreGive(g_mutex);
    if (settleMs) delay(settleMs);
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_radio.getCurrentReceivedSignalQuality();
    outRssi = g_radio.getCurrentRSSI();
    outSnr  = g_radio.getCurrentSNR();
    xSemaphoreGive(g_mutex);
}
