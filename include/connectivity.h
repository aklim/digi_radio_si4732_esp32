// ============================================================================
// connectivity.h — Bluetooth / WiFi power control + enable-flag tracking.
//
// This module owns the ESP32 BT controller and WiFi modem on behalf of the
// Settings menu. The Arduino-ESP32 core does not auto-start either radio at
// boot, but once app code references WiFi / BT APIs the default modes draw
// idle current. We force both into their lowest-power state at the top of
// setup() (see connectivityEarlyInit) and then re-apply the user's persisted
// preference once NVS has been loaded.
//
// Flag semantics (persisted as bt_en / wifi_en in NVS schema v4; both
// default to 0 on first boot for power-saving):
//   false — WiFi.mode(WIFI_OFF); BT controller stopped + deinit + BTDM
//           memory released. Header icons hidden.
//   true  — WiFi.mode(WIFI_STA) (no .begin()); BT controller initialized +
//           enabled in BLE mode (no advertising / no GATT). Header icons
//           drawn in TH.rf_icon (dim).
//
// `getBleStatus()` / `getWiFiStatus()` return an ATS-Mini-signature int8_t:
//   0 — off (icon is not drawn)
//   1 — enabled, not connected (icon drawn in TH.rf_icon)
//   2 — connected (icon drawn in TH.rf_icon_conn) — reserved for future PRs
//       that wire up real BLE GATT / WiFi STA connect.
// ============================================================================

#ifndef CONNECTIVITY_H
#define CONNECTIVITY_H

#include <stdint.h>

// Force BT and WiFi into their lowest-power state. Must be called once, very
// early in setup() — before any other code references WiFi / BT APIs and
// before persisted flags are applied. Idempotent but cheapest when called
// exactly once. Leaves the internal enable-flag shadows at their default
// (false); the persisted user preference is applied later via the setters.
void connectivityEarlyInit();

// Persist the flag and drive the underlying radio stack accordingly. Safe to
// call from any context (no RTOS primitives held). Idempotent — repeated
// calls with the same value re-assert the hardware state without side
// effects.
void connectivitySetBtEnabled(bool enabled);
void connectivitySetWifiEnabled(bool enabled);

// Live state — drives menu labels ("Bluetooth: On" / "Off").
bool connectivityGetBtEnabled();
bool connectivityGetWifiEnabled();

// ATS-Mini-signature status getters — consumed directly by drawBleIndicator
// / drawWiFiIndicator. See the file-level comment for the 0/1/2 legend.
int8_t getBleStatus();
int8_t getWiFiStatus();

#endif  // CONNECTIVITY_H
