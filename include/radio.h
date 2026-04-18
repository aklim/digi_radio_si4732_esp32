// ============================================================================
// radio.h — Si4732 FM receiver wrapper shared by the OLED and TFT firmwares.
//
// All Si4732 state (the PU2CLR library instance, RDS mirror buffers, cached
// signal-quality values) is owned by radio.cpp. main.cpp / main_tft.cpp talk
// to the chip only through this header.
//
// Threading: not thread-safe. All functions must be called from the Arduino
// loop task (same core that drives the I2C peripheral).
//
// I2C bus: radioInit() assumes Wire.begin(...) has already been called by the
// caller — this keeps pin choice out of the radio module.
//
// Rate limiting: radioPollSignal() and radioPollRds() self-rate-limit; they
// can safely be called every loop iteration.
// ============================================================================

#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

// --- FM band (frequency is in 10 kHz units to match the Si4735 library) -----
constexpr uint16_t FM_FREQ_MIN     = 8700;   // 87.0 MHz
constexpr uint16_t FM_FREQ_MAX     = 10800;  // 108.0 MHz
constexpr uint16_t FM_FREQ_DEFAULT = 10240;  // 102.4 MHz
constexpr uint16_t FM_FREQ_STEP    = 10;     // 100 kHz tuning step

// --- Volume -----------------------------------------------------------------
constexpr uint8_t DEFAULT_VOLUME = 30;
constexpr uint8_t MAX_VOLUME     = 63;

// Power up the Si4732 in FM mode and enable RDS. The chip must already be out
// of hardware reset (RC circuit). I2C (Wire) must already be initialised.
void radioInit();

// Tune / read frequency. Frequency is always in 10 kHz units.
void     radioSetFrequency(uint16_t freq10kHz);
uint16_t radioGetFrequency();

// Volume: 0..MAX_VOLUME. Values above MAX_VOLUME are clamped.
void    radioSetVolume(uint8_t v);
uint8_t radioGetVolume();

// Poll RSSI / SNR / stereo pilot. Self-rate-limited to 500 ms; callers can
// invoke this every loop iteration. Returns true when any cached value
// changed during this call — useful as a dirty-flag trigger for the UI.
bool    radioPollSignal();
uint8_t radioGetRssi();       // 0..127 dBuV
uint8_t radioGetSnr();        // dB
bool    radioIsStereo();      // pilot bit (FM stereo detect)

// Poll RDS. Self-rate-limited to 200 ms. Returns true when the local PS or RT
// mirror changed (new text, or stale text cleared after 10 s without sync).
bool        radioPollRds();
const char* radioGetRdsPs();  // up to 8 chars, "" when no sync / stale
const char* radioGetRdsRt();  // up to 64 chars, "" when no sync / stale

#endif  // RADIO_H
