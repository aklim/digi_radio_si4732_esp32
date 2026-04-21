// ============================================================================
// connectivity.cpp — Bluetooth / WiFi power control.
//
// Owns the ESP32 BT controller and WiFi modem lifecycle so the Settings
// menu's "Bluetooth" / "WiFi" toggles physically disable (and re-enable)
// the radios rather than just flipping a cosmetic flag.
//
// Key assumption: on Arduino-ESP32 the BT controller starts in IDLE and
// the WiFi driver starts in WIFI_OFF. We do **not** need to force either
// off at boot — they already are. `connectivityEarlyInit()` is preserved
// as a named landmark at the top of setup() for future lifecycle hooks,
// but it must never touch a radio in its default-uninitialised state:
// calls like `esp_bt_mem_release(ESP_BT_MODE_BTDM)` on an IDLE controller,
// or `esp_bt_controller_deinit()` on a never-initialised one, have been
// observed to panic/reboot the chip.
//
// "Enabled" today means "controller/modem is initialized and idle". No
// BLE GATT, no WiFi.begin() is called — those belong to future PRs. The
// int8_t status getters still reserve return value 2 for "connected", so
// drawBleIndicator / drawWiFiIndicator will bump to TH.rf_icon_conn (bright)
// automatically once a real stack lands and promotes the return value.
// ============================================================================

#include "connectivity.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_bt_main.h"

namespace {
bool g_btEnabled   = false;
bool g_wifiEnabled = false;

// Tear the BT controller down, but only if it was actually brought up.
// On a fresh boot the controller is in IDLE and this function is a no-op —
// any attempt to call esp_bt_controller_deinit() / esp_bt_mem_release()
// on a never-initialised controller is what was crashing the chip into a
// boot loop when this module first landed.
void btFullShutdown() {
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    if (st == ESP_BT_CONTROLLER_STATUS_IDLE) {
        return;  // Never initialised — nothing to undo.
    }
    btStop();
    st = esp_bt_controller_get_status();
    if (st == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
        st = esp_bt_controller_get_status();
    }
    if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_deinit();
    }
    // We deliberately do NOT call esp_bt_mem_release(BTDM) — it is a
    // one-way door that prevents any later re-enable, and has been
    // linked to boot-time panics on Arduino-ESP32 when invoked outside
    // the narrow "controller never initialised and never will be"
    // window.
}

// Bring the BT controller up in BLE-only mode. No advertising, no GATT —
// just enough so the radio is ready for future BLE code. BLE mode draws
// less current than dual-mode (BTDM) idle.
void btBleBringUp() {
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    if (st == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&cfg) != ESP_OK) return;
        st = esp_bt_controller_get_status();
    }
    if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
}
}  // namespace

void connectivityEarlyInit() {
    // Intentionally a no-op on fresh boot. BT is IDLE and WiFi is WIFI_OFF
    // by default on Arduino-ESP32, so no hardware call is required to reach
    // the lowest-power state — and aggressive calls here (esp_bt_mem_release,
    // forced WiFi.mode(WIFI_OFF) on a never-started driver) cause reboot
    // loops. Kept as a named function so setup() still advertises "BT/WiFi
    // baseline happens here" and future lifecycle work has an obvious home.
}

void connectivitySetBtEnabled(bool enabled) {
    g_btEnabled = enabled;
    if (enabled) {
        btBleBringUp();
    } else {
        btFullShutdown();
    }
}

void connectivitySetWifiEnabled(bool enabled) {
    g_wifiEnabled = enabled;
    if (enabled) {
        // STA mode only — no WiFi.begin() yet. Future PRs wiring up real
        // WiFi connectivity can add the SSID/pass handshake behind this flag.
        WiFi.mode(WIFI_STA);
    } else {
        // Only tear down if the driver was actually brought up. On fresh
        // boot the mode is already WIFI_OFF; calling WiFi.mode(WIFI_OFF)
        // unconditionally triggers a wifiLowLevelInit/deinit cycle that
        // can destabilise the radio task.
        if (WiFi.getMode() != WIFI_OFF) {
            WiFi.mode(WIFI_OFF);
        }
    }
}

bool connectivityGetBtEnabled()   { return g_btEnabled; }
bool connectivityGetWifiEnabled() { return g_wifiEnabled; }

int8_t getBleStatus()  { return g_btEnabled   ? 1 : 0; }
int8_t getWiFiStatus() { return g_wifiEnabled ? 1 : 0; }
