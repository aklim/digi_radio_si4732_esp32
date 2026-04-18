// ============================================================================
// radio.cpp — Si4732 FM receiver wrapper (RDS + signal-quality caching).
//
// The Si4732 RESET pin is NOT connected to any GPIO. An external RC circuit
// handles hardware reset at power-on, so the PU2CLR SI4735 library's normal
// setup() -> reset() flow is bypassed — we drive setPowerUp() + radioPowerUp()
// directly over I2C, matching the sequence that has been running reliably on
// the OLED firmware since v1.0.0.
//
// RDS mirror buffers: the library exposes getRdsText0A / getRdsText2A as
// char* into shared buffers that can change under us mid-read. We keep local
// mirrors (g_ps, g_rt) and diff against them on every poll so the UI gets a
// single clean "changed" signal per update.
// ============================================================================

#include "radio.h"

#include <Arduino.h>
#include <SI4735.h>
#include <string.h>

// --- Owned state (private to this translation unit) -------------------------
static SI4735 g_radio;

static uint16_t g_frequency = FM_FREQ_DEFAULT;
static uint8_t  g_volume    = DEFAULT_VOLUME;
static uint8_t  g_rssi      = 0;
static uint8_t  g_snr       = 0;
static bool     g_stereo    = false;

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
static char g_ps[9] = {0};
static char g_rt[65] = {0};

// ============================================================================
// Public API
// ============================================================================

void radioInit() {
    Serial.println(F("[radio] initializing Si4732 (no reset pin, RC-driven)..."));

    // SEN pin wired to GND -> I2C address 0x11 (library index 0).
    g_radio.setDeviceI2CAddress(0);

    // CTSIEN=0, GPO2OEN=0, PATCH=0, XOSCEN=1, FUNC=FM, OPMODE=analog audio.
    g_radio.setPowerUp(0, 0, 0, XOSCEN_CRYSTAL, POWER_UP_FM, SI473X_ANALOG_AUDIO);
    g_radio.radioPowerUp();

    // Crystal oscillator stabilisation.
    delay(250);

    g_radio.setVolume(DEFAULT_VOLUME);
    g_radio.getFirmware();

    g_radio.setFM(FM_FREQ_MIN, FM_FREQ_MAX, FM_FREQ_DEFAULT, FM_FREQ_STEP);

    // Enable RDS with maximum block-error tolerance. The four threshold args
    // (BLETHA..D) are the library's defaults for permissive reception — more
    // data at the cost of occasional garbled characters, which is the right
    // tradeoff for a portable FM receiver.
    g_radio.setRdsConfig(1, 3, 3, 3, 3);

    g_frequency = g_radio.getFrequency();
    g_volume    = g_radio.getVolume();

    Serial.print(F("[radio] Si4732 ready. Firmware PN: "));
    Serial.println(g_radio.getFirmwarePN());
    Serial.print(F("[radio] Tuned to: "));
    Serial.print(g_frequency / 100.0, 1);
    Serial.println(F(" MHz"));
}

void radioSetFrequency(uint16_t freq10kHz) {
    g_radio.setFrequency(freq10kHz);
    g_frequency = freq10kHz;

    // Drop any text from the previous station. The library's own RDS buffers
    // are protected (can't memset from here), but we only copy from them when
    // getRdsSync() is true — and sync is lost on tune, so the library's stale
    // bytes won't leak into our mirrors before the new station locks in.
    g_ps[0] = 0;
    g_rt[0] = 0;
    g_lastRdsSync = 0;
}

uint16_t radioGetFrequency() { return g_frequency; }

void radioSetVolume(uint8_t v) {
    if (v > MAX_VOLUME) v = MAX_VOLUME;
    g_radio.setVolume(v);
    g_volume = v;
}

uint8_t radioGetVolume() { return g_volume; }

bool radioPollSignal() {
    unsigned long now = millis();
    if (now - g_lastSignalPoll < SIGNAL_POLL_INTERVAL_MS) return false;
    g_lastSignalPoll = now;

    g_radio.getCurrentReceivedSignalQuality();
    uint8_t rssi   = g_radio.getCurrentRSSI();
    uint8_t snr    = g_radio.getCurrentSNR();
    bool    stereo = g_radio.getCurrentPilot();

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
