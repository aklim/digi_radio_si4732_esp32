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

// Short band-mode label for the mode box. Corresponds to ATS-Mini's
// `bandModeDesc[currentMode]`.
static const char *modeText(BandMode m) {
    switch (m) {
        case MODE_FM:  return "FM";
        case MODE_AM:  return "AM";
        default:       return "--";
    }
}

// Linear RSSI-to-strength map: 0..60 dBuV -> 0..49 segments. ATS-Mini's
// upstream uses a per-modulation threshold table (AM vs FM vs SSB); we'll
// port that once drawSMeter gains its own config step. This linear stub is
// functionally correct and keeps the bar lively during normal tuning.
static int strengthFromRssi(uint8_t rssi) {
    int s = (int)rssi * 49 / 60;
    if (s < 0) s = 0;
    if (s > 49) s = 49;
    return s;
}

void drawLayoutDefault() {
    // drawSaveIndicator / drawBleIndicator — no BLE save path in this build.

    // Battery widget (stub — always "on battery" with placeholder voltage).
    drawBattery(BATT_OFFSET_X, BATT_OFFSET_Y);

    // drawWiFiIndicator — no WiFi in this build.

    // Band + modulation box. drawBandAndMode selects Orbitron_Light_24
    // internally, so Layout-Default does not need its own TFT handle.
    const Band *band = radioGetCurrentBand();
    drawBandAndMode(band->name, modeText(band->mode), BAND_OFFSET_X, BAND_OFFSET_Y);

    // Frequency + unit (MHz for FM, kHz for AM/MW/SW).
    uint16_t freq = radioGetFrequency();
    drawFrequency((uint32_t)freq,
                  FREQ_OFFSET_X, FREQ_OFFSET_Y,
                  FUNIT_OFFSET_X, FUNIT_OFFSET_Y);

    // RDS station name (PS). Upstream has a separate long-name path for
    // the EIBI database; we only ever have the 8-char PS string.
    char ps[9];
    radioGetRdsPs(ps, sizeof(ps));
    if (ps[0]) {
        drawStationName(ps, RDS_OFFSET_X, RDS_OFFSET_Y);
    }

    // S-meter (top edge) + stereo pilot split.
    int strength = strengthFromRssi(radioGetRssi());
    drawSMeter(strength, METER_OFFSET_X, METER_OFFSET_Y);
    drawStereoIndicator(METER_OFFSET_X, METER_OFFSET_Y,
                        (band->mode == MODE_FM) && radioIsStereo());

    // Bottom status row: RadioText if we have any, otherwise the band
    // scale (Step 5) will claim this space. For now just draw RT when
    // present and leave the scale slot blank.
    char rt[65];
    radioGetRdsRt(rt, sizeof(rt));
    if (rt[0]) {
        drawRadioText(STATUS_OFFSET_Y, STATUS_OFFSET_Y + 25);
    }
}
