// ============================================================================
// connectivity.cpp — Bluetooth / WiFi enable-flag scaffold.
//
// See connectivity.h for the design rationale: this build only tracks the
// user's on/off preference and gates the header indicator icons. The real
// stacks land in future PRs.
//
// When WiFi STA lands, connectivitySetWifiEnabled(true) should call
// WiFi.mode(WIFI_STA) + WiFi.begin(ssid, pass), transition to status 2
// once WiFi.status()==WL_CONNECTED, and undo on disable. BLE serial /
// A2DP follows the same enable → listen → on-connect-bump-to-2 pattern.
// ============================================================================

#include "connectivity.h"

namespace {
bool g_btEnabled   = false;
bool g_wifiEnabled = false;
}  // namespace

void connectivitySetBtEnabled(bool enabled) {
    g_btEnabled = enabled;
    // Future: start/stop BTDevice here. No-op today — the icon is the only
    // user-visible effect of flipping this flag.
}

void connectivitySetWifiEnabled(bool enabled) {
    g_wifiEnabled = enabled;
    // Future: WiFi.mode(...) / WiFi.begin(...) / WiFi.disconnect() here.
}

bool connectivityGetBtEnabled()   { return g_btEnabled; }
bool connectivityGetWifiEnabled() { return g_wifiEnabled; }

int8_t getBleStatus()  { return g_btEnabled   ? 1 : 0; }
int8_t getWiFiStatus() { return g_wifiEnabled ? 1 : 0; }
