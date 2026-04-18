// ============================================================================
// Battery.h — battery-monitor interface (stubbed).
//
// Thin facade so Draw.cpp can paint the battery icon without caring whether
// the reading is real or a constant. Backed by a placeholder in Battery.cpp
// until LiPo + voltage divider hardware lands.
// ============================================================================

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

float   batteryGetVolts();
uint8_t batteryGetSocPercent();   // 0..100
bool    batteryIsCharging();

#endif  // BATTERY_H
