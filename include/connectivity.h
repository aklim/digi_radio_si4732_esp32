// ============================================================================
// connectivity.h — Bluetooth / WiFi enable-flag scaffold.
//
// This module is intentionally tiny today: it only tracks whether the user
// has enabled Bluetooth and/or WiFi in the Settings menu. The real ESP32
// radio stacks (BLE serial / A2DP sink on the BT side; STA / AP on the
// WiFi side) are not brought up in this build — that belongs to separate,
// much larger PRs.
//
// The `getBleStatus()` / `getWiFiStatus()` functions return a 3-valued
// int8_t matching the ATS-Mini signature (0 = off, 1 = enabled but not
// connected, 2 = connected to a peer/AP). In this build the return value
// is always 0 or 1 based solely on the persisted enable flag. Future
// connectivity PRs upgrade the return to 2 when the real stack reports a
// link — the header indicator widgets (Draw.cpp) already switch from
// TH.rf_icon (dim) to TH.rf_icon_conn (bright) on status==2, so no UI
// changes will be needed when that lands.
// ============================================================================

#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <stdint.h>

// Persist the flag and (eventually) start / stop the underlying radio
// stack. Safe to call from any context; no RTOS primitives are held.
void connectivitySetBtEnabled(bool enabled);
void connectivitySetWifiEnabled(bool enabled);

// Live state — drives menu labels ("Bluetooth: On" / "Off").
bool connectivityGetBtEnabled();
bool connectivityGetWifiEnabled();

// ATS-Mini-signature status getters — consumed directly by drawBleIndicator
// / drawWiFiIndicator. Returns:
//   0 — off (icon is not drawn)
//   1 — enabled, not connected (icon drawn in TH.rf_icon)
//   2 — connected (icon drawn in TH.rf_icon_conn)
int8_t getBleStatus();
int8_t getWiFiStatus();

#endif  // CONNECTIVITY_H
