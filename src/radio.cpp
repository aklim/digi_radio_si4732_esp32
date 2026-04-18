// ============================================================================
// radio.cpp — Si4732 multi-band receiver wrapper (FM + MW/SW AM).
//
// The Si4732 RESET pin is NOT connected to any GPIO. An external RC circuit
// handles hardware reset at power-on, so the PU2CLR SI4735 library's normal
// setup() -> reset() flow is bypassed — we drive setPowerUp() + radioPowerUp()
// directly over I2C, matching the sequence that has been running reliably on
// the OLED firmware since v1.0.0.
//
// Band layout ported from ATS-Mini's Common.h + Menu.cpp (see
// https://github.com/esp32-si4732/ats-mini). We start with four bands (FM
// Broadcast, MW, SW 41m, SW 31m); LW and additional SW segments plus SSB
// are tracked separately in docs/future_improvements.md and will land in
// follow-up PRs. Index 0 is FM Broadcast so the OLED firmware (which never
// calls radioSetBand()) gets its historical behaviour for free.
//
// RDS mirror buffers: the library exposes getRdsText0A / getRdsText2A as
// char* into shared buffers that can change under us mid-read. We keep local
// mirrors (g_ps, g_rt) and diff against them on every poll so the UI gets a
// single clean "changed" signal per update. RDS polling is disabled while on
// an AM band; the Si4732 doesn't decode RDS off-FM and polling would just be
// wasted I2C bandwidth.
// ============================================================================

#include "radio.h"

#include <Arduino.h>
#include <SI4735.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Band table
//
// FM Broadcast stays in 10 kHz units (Si4735 FM convention).
// AM / MW / SW stay in 1 kHz units (Si4735 AM convention).
// `step` is the encoder detent size in the band's native units.
// ============================================================================

Band g_bands[] = {
    // FM Broadcast 87.0 – 108.0 MHz, 100 kHz steps. Matches v1 behaviour.
    { "FM Broadcast", BAND_FM, MODE_FM, 8700, 10800, 10240, 10240, 10 },
    // MW (AM broadcast) 520 – 1710 kHz, 10 kHz steps. Region-neutral (9 kHz
    // for ITU region 1 is a later PR behind a settings toggle).
    { "MW",           BAND_MW, MODE_AM,  520,  1710,  1000,  1000, 10 },
    // SW 41 m amateur/broadcast 7100 – 7300 kHz, 5 kHz steps.
    { "SW 41m",       BAND_SW, MODE_AM, 7100,  7300,  7200,  7200,  5 },
    // SW 31 m broadcast 9400 – 9900 kHz, 5 kHz steps.
    { "SW 31m",       BAND_SW, MODE_AM, 9400,  9900,  9700,  9700,  5 },
};

const size_t g_bandCount = sizeof(g_bands) / sizeof(g_bands[0]);

static uint8_t g_bandIdx = 0;

// --- Owned state (private to this translation unit) -------------------------
static SI4735 g_radio;

static uint8_t g_volume = DEFAULT_VOLUME;
static uint8_t g_rssi   = 0;
static uint8_t g_snr    = 0;
static bool    g_stereo = false;

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
static char g_ps[9]  = {0};
static char g_rt[65] = {0};

// --- Internal helpers -------------------------------------------------------

static Band& activeBand() { return g_bands[g_bandIdx]; }

// Apply the current band's settings to the Si4735. Called from radioInit()
// and radioSetBand(). Separates chip reconfiguration from the bookkeeping
// (volume re-apply, mirror clear) that both call sites need.
static void applyBandToRadio() {
    Band& b = activeBand();

    if (b.mode == MODE_FM) {
        g_radio.setFM(b.minFreq, b.maxFreq, b.currentFreq, b.step);
        // Permissive RDS block-error thresholds — more data at the cost of
        // occasional garbled characters, which is the right tradeoff for a
        // portable FM receiver. Same as v1.
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
}

// ============================================================================
// Public API
// ============================================================================

void radioInit() {
    Serial.println(F("[radio] initializing Si4732 (no reset pin, RC-driven)..."));

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

    applyBandToRadio();

    Band& b = activeBand();
    b.currentFreq = g_radio.getFrequency();
    g_volume      = g_radio.getVolume();

    Serial.print(F("[radio] Si4732 ready. Firmware PN: "));
    Serial.println(g_radio.getFirmwarePN());
    Serial.print(F("[radio] Band: "));
    Serial.print(b.name);
    Serial.print(F("  Tuned to: "));
    Serial.println(b.currentFreq);
}

void radioSetBand(uint8_t idx) {
    if (idx >= g_bandCount) return;
    if (idx == g_bandIdx) return;

    // Snapshot the outgoing band's tune so we can restore on return.
    activeBand().currentFreq = g_radio.getFrequency();

    g_bandIdx = idx;
    applyBandToRadio();

    // Swap to a fresh, empty signal cache so the UI redraws from "no data"
    // rather than showing the previous band's last RSSI/SNR until the next
    // 500 ms poll fires.
    g_rssi   = 0;
    g_snr    = 0;
    g_stereo = false;
    g_lastSignalPoll = 0;

    // RDS is meaningless off-FM; clear the mirrors so any UI that peeks at
    // them sees empty strings instead of the last FM station's text.
    g_ps[0] = 0;
    g_rt[0] = 0;
    g_lastRdsSync = 0;

    Band& b = activeBand();
    Serial.print(F("[radio] Band switch -> "));
    Serial.print(b.name);
    Serial.print(F("  freq="));
    Serial.println(b.currentFreq);
}

uint8_t radioGetBandIdx() { return g_bandIdx; }

const Band* radioGetCurrentBand() { return &g_bands[g_bandIdx]; }

void radioSetFrequency(uint16_t freq) {
    Band& b = activeBand();
    if (freq < b.minFreq) freq = b.minFreq;
    if (freq > b.maxFreq) freq = b.maxFreq;

    g_radio.setFrequency(freq);
    b.currentFreq = freq;

    // Drop any text from the previous station. The library's own RDS buffers
    // are protected (can't memset from here), but we only copy from them when
    // getRdsSync() is true — and sync is lost on tune, so the library's stale
    // bytes won't leak into our mirrors before the new station locks in.
    if (b.mode == MODE_FM) {
        g_ps[0] = 0;
        g_rt[0] = 0;
        g_lastRdsSync = 0;
    }
}

uint16_t radioGetFrequency() { return activeBand().currentFreq; }

void radioSetVolume(uint8_t v) {
    if (v > MAX_VOLUME) v = MAX_VOLUME;
    g_radio.setVolume(v);
    g_volume = v;
}

uint8_t radioGetVolume() { return g_volume; }

void radioFormatFrequency(char* buf, size_t bufsize) {
    if (!buf || bufsize < 2) return;
    const Band& b = activeBand();

    if (b.mode == MODE_FM) {
        // Stored in 10 kHz units; render as MHz with one decimal.
        snprintf(buf, bufsize, "%u.%u MHz",
                 b.currentFreq / 100,
                 (b.currentFreq % 100) / 10);
    } else {
        // Stored in 1 kHz units. MW/SW show whole kHz; LW would look odd in
        // kHz (e.g. "198 kHz" broadcast) but the same format works there
        // too, so we unify.
        snprintf(buf, bufsize, "%u kHz", b.currentFreq);
    }
}

bool radioPollSignal() {
    unsigned long now = millis();
    if (now - g_lastSignalPoll < SIGNAL_POLL_INTERVAL_MS) return false;
    g_lastSignalPoll = now;

    g_radio.getCurrentReceivedSignalQuality();
    uint8_t rssi   = g_radio.getCurrentRSSI();
    uint8_t snr    = g_radio.getCurrentSNR();
    // getCurrentPilot() is only meaningful on FM; force mono on AM bands so
    // the stereo dot in the UI doesn't flicker with noise.
    bool    stereo = (activeBand().mode == MODE_FM) && g_radio.getCurrentPilot();

    bool changed = (rssi != g_rssi) || (snr != g_snr) || (stereo != g_stereo);
    g_rssi   = rssi;
    g_snr    = snr;
    g_stereo = stereo;
    return changed;
}

uint8_t radioGetRssi()   { return g_rssi; }
uint8_t radioGetSnr()    { return g_snr; }
bool    radioIsStereo()  { return g_stereo; }

bool radioPollRds() {
    // No RDS on AM bands — short-circuit before spending any I2C bandwidth.
    if (activeBand().mode != MODE_FM) return false;

    unsigned long now = millis();
    if (now - g_lastRdsPoll < RDS_POLL_INTERVAL_MS) return false;
    g_lastRdsPoll = now;

    bool changed = false;

    // INTACK=0, MTFIFO=0, STATUSONLY=0 — normal status read with ack of queued data.
    g_radio.getRdsStatus();

    if (g_radio.getRdsSync()) {
        g_lastRdsSync = now;

        if (g_radio.getRdsReceived()) {
            char* libPs = g_radio.getRdsStationName();  // 0A group -> PS
            char* libRt = g_radio.getRdsText2A();       // 2A group -> RadioText

            if (libPs && strncmp(g_ps, libPs, 8) != 0) {
                strncpy(g_ps, libPs, 8);
                g_ps[8] = 0;
                changed = true;
            }
            if (libRt && strncmp(g_rt, libRt, 64) != 0) {
                strncpy(g_rt, libRt, 64);
                g_rt[64] = 0;
                changed = true;
            }
        }
    } else if (g_lastRdsSync && (now - g_lastRdsSync > RDS_STALE_MS)) {
        // Lost sync long enough that any displayed text is almost certainly
        // from a different station. Clear so the UI shows "no RDS" instead of
        // a stale PS name.
        if (g_ps[0] || g_rt[0]) {
            g_ps[0] = 0;
            g_rt[0] = 0;
            changed = true;
        }
    }

    return changed;
}

const char* radioGetRdsPs() { return g_ps; }
const char* radioGetRdsRt() { return g_rt; }
