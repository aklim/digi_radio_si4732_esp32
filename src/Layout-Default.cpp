// ============================================================================
// Layout-Default.cpp — full-screen widget orchestration.
//
// Ported from ats-mini/Layout-Default.cpp. The upstream function chains a
// fixed sequence of widget draws that together paint the main screen; our
// port keeps the same sequence so the visible layout matches ATS-Mini for
// every feature we already have.
//
// Features not yet implemented are stubbed or skipped:
//   - drawSaveIndicator / drawBleIndicator / drawWiFiIndicator /
//     drawWiFiStatus — no BLE / WiFi hardware yet.
//   - drawSideBar (Step 5), drawScale (Step 5), drawScanGraphs (Step 7).
//   - drawLongStationName (EIBI database not ported).
//   - digit-edit underline highlight in drawFrequency.
//   - switchThemeEditor live-preview.
// ============================================================================

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "Draw.h"
#include "Themes.h"
#include "radio.h"
#include "Scan.h"
#include "input.h"

// Short band-mode label for the mode box. Corresponds to ATS-Mini's
// `bandModeDesc[currentMode]`.
static const char *modeText(BandMode m) {
    switch (m) {
        case MODE_FM:  return "FM";
        case MODE_AM:  return "AM";
        default:       return "--";
    }
}

// Short display form of a band name. Our radio.h spells bands out
// ("FM Broadcast", "SW 41m", ...) which overflows the drawBandAndMode
// slot; ATS-Mini uses a short `bandName` field. Here we synthesise it
// by taking the first whitespace-delimited word ("FM", "MW", "SW"),
// which is the visible substring upstream uses for the same bands.
static void shortBandName(const char *full, char *buf, size_t n) {
    if (n == 0) return;
    size_t i = 0;
    while (i + 1 < n && full[i] && full[i] != ' ') {
        buf[i] = full[i];
        i++;
    }
    buf[i] = '\0';
}

// RSSI → S-point conversion, ported 1:1 from ATS-Mini's Utils.cpp
// (getStrength, ats-mini/Utils.cpp:417-458). Two step-tables — one for
// FM, one for AM/SW — map dBuV values to S0..S9+60 (strength 1..17).
// drawSMeter renders `strength` segments of 4 px pitch starting at x+15;
// the stereo indicator mask is sized exactly for 17 segments, so keeping
// strength inside 1..17 is what makes the split-bar effect look right.
//
// Previous stub returned 0..49 (linear 0..60 dBuV), which made the meter
// three times longer than ATS-Mini's and left the stereo stripe only
// covering the first third of the bar.
static int strengthFromRssi(uint8_t rssi) {
    const Band *band = radioGetCurrentBand();
    if (band && band->mode == MODE_FM) {
        if (rssi <=  1) return  1;   // S0
        if (rssi <=  2) return  7;   // S6
        if (rssi <=  8) return  8;   // S7
        if (rssi <= 14) return  9;   // S8
        if (rssi <= 24) return 10;   // S9
        if (rssi <= 34) return 11;   // S9 +10
        if (rssi <= 44) return 12;   // S9 +20
        if (rssi <= 54) return 13;   // S9 +30
        if (rssi <= 64) return 14;   // S9 +40
        if (rssi <= 74) return 15;   // S9 +50
        if (rssi <= 76) return 16;   // S9 +60
        return               17;     // >S9 +60
    }
    // AM / SW / MW
    if (rssi <=  1) return  1;
    if (rssi <=  2) return  2;
    if (rssi <=  3) return  3;
    if (rssi <=  4) return  4;
    if (rssi <= 10) return  5;
    if (rssi <= 16) return  6;
    if (rssi <= 22) return  7;
    if (rssi <= 28) return  8;
    if (rssi <= 34) return  9;
    if (rssi <= 44) return 10;
    if (rssi <= 54) return 11;
    if (rssi <= 64) return 12;
    if (rssi <= 74) return 13;
    if (rssi <= 84) return 14;
    if (rssi <= 94) return 15;
    if (rssi <= 95) return 16;
    return                 17;
}

void drawLayoutDefault() {
    // drawSaveIndicator / drawBleIndicator — no BLE save path in this build.

    // Battery widget (stub — always "on battery" with placeholder voltage).
    drawBattery(BATT_OFFSET_X, BATT_OFFSET_Y);

    // drawWiFiIndicator — no WiFi in this build.

    // Band + modulation box. drawBandAndMode selects Orbitron_Light_24
    // internally, so Layout-Default does not need its own TFT handle.
    // radioGetCurrentBand()->name is the long form ("FM Broadcast"); the
    // mode box sits immediately to the right of the band tag so we
    // shorten it before drawing.
    const Band *band = radioGetCurrentBand();
    char shortName[8];
    shortBandName(band->name, shortName, sizeof(shortName));
    drawBandAndMode(shortName, modeText(band->mode), BAND_OFFSET_X, BAND_OFFSET_Y);

    // Frequency + unit (MHz for FM, kHz for AM/MW/SW).
    uint16_t freq = radioGetFrequency();
    drawFrequency((uint32_t)freq,
                  FREQ_OFFSET_X, FREQ_OFFSET_Y,
                  FUNIT_OFFSET_X, FUNIT_OFFSET_Y);

    // RDS station name (PS). Always call so the draw function's
    // per-widget clear runs even when PS is empty — that way a sync
    // drop paints the zone blank instead of leaving the old name.
    char ps[9];
    radioGetRdsPs(ps, sizeof(ps));
    drawStationName(ps, RDS_OFFSET_X, RDS_OFFSET_Y);

    // Left sidebar — info box with Step/BW/AGC/Vol/PI/Time rows.
    // Upstream's x/y offsets land a 86×110 box at (0, 18). Rendered
    // before the S-meter overlays so the meter's icon column stays on
    // top of the box's rounded corner.
    drawSideBar(MENU_OFFSET_X, MENU_OFFSET_Y, MENU_DELTA_X,
                getAdjustMode() == MODE_VOLUME);

    // S-meter (top edge) + stereo pilot split.
    int strength = strengthFromRssi(radioGetRssi());
    drawSMeter(strength, METER_OFFSET_X, METER_OFFSET_Y);
    drawStereoIndicator(METER_OFFSET_X, METER_OFFSET_Y,
                        (band->mode == MODE_FM) && radioIsStereo());

    // Bottom area (y >= 120): priority order matches upstream —
    //   1. Scan graph while a sweep is active or its data is held.
    //   2. RadioText if the station is sending RT — radio.cpp's
    //      sanitiser guarantees g_rt is either empty or real printable
    //      text, so a simple rt[0] test is authoritative here.
    //   3. Otherwise the static band scale.
    if (scanIsActive()) {
        drawScanGraphs(radioGetFrequency());
    } else {
        char rt[65];
        radioGetRdsRt(rt, sizeof(rt));
        if (rt[0]) drawRadioText(STATUS_OFFSET_Y, STATUS_OFFSET_Y + 25);
        else       drawScale(radioGetFrequency());
    }
}
