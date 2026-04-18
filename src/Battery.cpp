// ============================================================================
// Battery.cpp — stub battery readout.
//
// Our hardware has no LiPo + voltage divider yet (see docs/hardware.md and
// the roadmap entry in docs/future_improvements.md). This file supplies a
// placeholder so Layout-Default renders the battery icon with a plausible
// value; it will be replaced with a real ADC read once the divider ships.
//
// Fixed values: 4.15 V, ~80 % SOC, not charging — picked to land the icon
// in the "full" colour zone of ATS-Mini's drawBattery() thresholds.
// ============================================================================

#include "Battery.h"

float   batteryGetVolts()    { return 4.15f; }
uint8_t batteryGetSocPercent() { return 80;   }
bool    batteryIsCharging()  { return false; }
