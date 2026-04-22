# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Multi-band Si4732 radio receiver firmware for ESP32 with a Waveshare 2.8" ST7789V TFT touch shield and a rotary encoder. Bands currently: FM Broadcast, MW, SW 41 m, SW 31 m. Input is rotary encoder (click = FREQ ↔ VOL, long-press = menu) plus XPT2046 touch for quick mode selection. Runtime state (band / per-band freq / volume) persists to NVS. Built with PlatformIO and the Arduino framework.

North star: feature / UI parity with [ATS-Mini](https://github.com/esp32-si4732/ats-mini) plus the dual-core task split from H.-J. Berndt's pocketSI4735DualCoreDecoder. Roadmap in [docs/future_improvements.md](docs/future_improvements.md). The legacy 128×64 SSD1315 OLED build was retired so all effort goes into the TFT UX — **do not re-introduce OLED code**.

## Build Commands

```bash
pio run -e esp32dev                       # Build firmware
pio run -e esp32dev -t upload             # Build + flash
pio run -e shield_test -t upload          # TFT shield bring-up fixture (not a product)
pio run -e esp32dev --target size         # Program size
pio test -e native                        # Run native host unit tests (Unity)
```

## Architecture

- **Build system:** PlatformIO (`platformio.ini`) with a `[common]` base section and three envs: `esp32dev` (product), `shield_test` (bring-up fixture), and `native` (host-side unit tests via Unity). The hardware envs use an explicit `build_src_filter` to control which `src/*.cpp` files they pull in; `native` enables `test_build_src = yes` plus a narrow `build_src_filter` so it links only the pure helper cpps.
- **Framework:** Arduino (esp32dev / shield_test); native for test env.
- **Sources:**
  - `src/main.cpp` — Arduino entry (`setup` / `loop`), TFT drawing, touch dispatch, menu/input routing.
  - `src/radio.cpp` + `include/radio.h` — Si4732 wrapper: `radioSetBand()`, tune / volume / RDS / signal polling, `radioFormatFrequency()` (mode-aware unit, delegates to the pure helper in `include/radio_format.h` under the mutex). Also exposes `radioSetRdsEnabled()` / `radioGetRdsEnabled()` / `radioIsRdsSyncing()` — the user-controlled RDS gate that short-circuits both the chip's decoder and the Core-0 poll loop.
  - `src/band_table.cpp` + `include/radio_bands.h` — `Band` struct + `BandType` / `BandMode` enums + `g_bands[]` definitions. Split out of `radio.cpp` so the native test env can link it without Arduino / SI4735 / FreeRTOS.
  - `src/rds_sanitize.cpp` + `include/rds_sanitize.h` — `rdsSanitizeRt()` (ATS-Mini-style printable-text gate for the PU2CLR library's RadioText buffer). Split out of `radio.cpp` for the same reason.
  - `include/radio_format.h` — `radioFormatFrequencyPure()` inline helper (pure string build; the thread-safe wrapper in `radio.cpp` snapshots mode + freq under the mutex, then calls this).
  - `include/preset_pack.h` — `PresetSlot` struct + `presetPack()` / `presetUnpack()` inline helpers for the memory-preset NVS encoding (one packed `uint32_t` per slot). Pure, no Arduino deps — shared between `persist.cpp` and the native test suite.
  - `src/input.cpp` + `include/input.h` — rotary-encoder wrapper with `ButtonEvent { BTN_NONE, BTN_CLICK, BTN_LONG_PRESS }` (500 ms threshold) and `encoderSetBoundsForMenu()`.
  - `src/menu.cpp` + `include/menu.h` — modal menu state machine (`MENU_TOP` → `MENU_BAND` / `MENU_BW` / `MENU_AGC` / `MENU_THEME` / `MENU_SETTINGS` → `MENU_BRIGHTNESS` / `MENU_MEMORY` → `MENU_MEMORY_SLOT`) with a scrolling list viewport; GFX-free-font rendering. `MENU_SETTINGS` holds the RDS / Bluetooth / WiFi on-off toggles and, unlike the other pickers, stays open after a click so the user can flip multiple toggles in one visit. `MENU_MEMORY` lists the 16 preset slots; clicking one descends into `MENU_MEMORY_SLOT` which offers Load / Save current / Delete / Back — Save/Delete return to the slot list so the user sees the updated label immediately, Load closes the menu to get out of the user's way.
  - `src/persist.cpp` + `include/persist.h` — versioned NVS (Preferences) wrapper with rate-limited writes. Schema guarded by `PERSIST_SCHEMA_VER` (v6 — adds 16 `preset<N>` u32 keys on top of v5's `bl_level`, v4's `rds_en` / `bt_en` / `wifi_en`, and v3's `bw_fm` / `bw_am` / `agc_fm` / `agc_am`). Additive migrations from v1 / v2 / v3 / v4 / v5 preserve pre-existing state.
  - `src/connectivity.cpp` + `include/connectivity.h` — Bluetooth / WiFi power control. Owns the ESP32 BT controller and WiFi modem lifecycle: `connectivitySetBtEnabled()` / `connectivitySetWifiEnabled()` physically (de)initialize the radios to match the user's Settings-menu choice. BT off → `btStop()` + `esp_bt_controller_disable()` + `esp_bt_controller_deinit()`, skipped entirely if the controller is still in `IDLE` state (never initialized). BT on → BLE-only bring-up (`esp_bt_controller_init` + `esp_bt_controller_enable(ESP_BT_MODE_BLE)`, no advertising / no GATT). WiFi uses `WiFi.mode(WIFI_STA / WIFI_OFF)` — the OFF branch is skipped when `WiFi.getMode()` is already `WIFI_OFF` to avoid a spurious `wifiLowLevelInit/deinit` cycle. Do **not** call `esp_bt_mem_release(ESP_BT_MODE_BTDM)` anywhere — it is a one-way door and has been observed to panic the chip into a boot loop. `connectivityEarlyInit()` exists as a named landmark at the very top of `setup()` but is intentionally a no-op: BT is `IDLE` and WiFi is `WIFI_OFF` by default on Arduino-ESP32, so no hardware call is needed to reach the lowest-power state. The persisted on/off flag lives in NVS (`bt_en` / `wifi_en`, default 0). `getBleStatus()` / `getWiFiStatus()` still return `0` / `1` today (never `2` — "connected" is reserved for future PRs that wire real GATT / STA connect).
  - `src/Draw.cpp` + `include/Draw.h` — ATS-Mini widget draws ported 1:1: battery + voltage, band/mode tag, frequency + unit, RDS station name (PS), RDS RadioText (RT), S-meter + stereo indicator, band scale, bandscope scan graphs, sidebar info box, and the header status indicators (`drawBleIndicator`, `drawWiFiIndicator`, `drawRdsIndicator`). All functions push pixels through the shared `g_tft` handle registered by `drawInit()`.
  - `src/Layout-Default.cpp` — full-screen widget orchestrator. Calls every `drawXxx` in the ATS-Mini-parity order on each repaint; owns the Y-priority switch between `drawScanGraphs` / `drawRadioText` / `drawScale` in the lower half.
  - `src/Battery.cpp` + `include/Battery.h` — battery voltage / SOC stub. Always reports "on battery" with placeholder values until the real voltage-divider hardware lands.
  - `src/Scan.cpp` + `include/Scan.h` — bandscope sweep buffer + state machine consumed by `drawScanGraphs`.
  - `src/Seek.cpp` + `include/Seek.h` — auto-seek state machine. Tick-driven like `Scan.cpp` and built on the same `radioScanEnter / radioScanMeasure / radioScanExit` primitives, so seek and scan share the Core-0 poll gate but never run concurrently. Stops on the first frequency whose RSSI + SNR meet per-mode thresholds (FM: `RSSI≥15 / SNR≥8`; AM/SW: `RSSI≥25 / SNR≥8`), wraps at band edges, restores the origin tune if the full band has no hit. The wrap arithmetic is factored into `seekNextFreq()` in [include/seek_step.h](include/seek_step.h) so the native test env can link it. Triggered from the bottom touch-button row, not from the menu.
  - `src/Themes.cpp` + `include/Themes.h` — ATS-Mini-parity palette catalogue (Default / Bluesky / eInk / Pager / Orange / Night / Phosphor / Space / Magenta). `TH` macro resolves to the active theme struct; every draw function references it so theme switches apply on the next repaint.
  - `src/test_shield.cpp` — TFT shield bring-up fixture (not a product).
  - `include/ui_layout.h` — UI coordinates, colours, font choices.
  - `include/User_Setup.h` — TFT_eSPI configuration (force-included via platformio.ini).
- **Libraries:** PU2CLR SI4735, TFT_eSPI (also covers XPT2046 touch), Ai Esp32 Rotary Encoder.
- **Tests:** `test/test_native_format/`, `test/test_native_rds/`, `test/test_native_bands/`, `test/test_native_preset/`, `test/test_native_seek/` — Unity suites for `radioFormatFrequencyPure`, `rdsSanitizeRt`, `g_bands[]` invariants, the memory-preset pack/unpack codec, and the auto-seek wrap arithmetic. Run with `pio test -e native` (~7 s, no hardware needed). Gated in CI via [.github/workflows/ci.yml](.github/workflows/ci.yml) on push + PR to master. Time-mocked units (encoder click/long-press SM, persist rate-limiting) are deferred pending a `millis()` injection seam — see [docs/future_improvements.md](docs/future_improvements.md) "Release / CI".
- **Docs:** `docs/` — hardware wiring, firmware architecture (includes tests section), TFT UI spec, menu spec, shield bring-up, future-improvements roadmap, release runbook.

## Key Implementation Details

- The Si4732 RESET pin is **not connected** to any GPIO. An external RC circuit handles hardware reset. `radio.cpp` bypasses the PU2CLR library's `setup()` → `reset()` flow and calls `radioPowerUp()` directly over I2C.
- Si4732 I2C address is 0x11 (SEN pin wired to GND), set via `setDeviceI2CAddress(0)`.
- Pin split (see [docs/hardware.md](docs/hardware.md)):
  - Si4732 + I²C : SDA=21, SCL=22
  - Encoder     : A=18, B=19, BTN=5
  - TFT HSPI    : MOSI=13, SCLK=14, MISO=27, CS=15, DC=2, RST=33
  - Backlight   : GPIO 4 (LEDC PWM)
  - Touch CS    : GPIO 17 (XPT2046)
- Encoder uses ISR (`IRAM_ATTR`) on pins 18/19/5 for responsive input; acceleration is enabled. Long-press is detected by `encoderPollButton()` via `isEncoderButtonDown()` + timestamp (the underlying library only exposes click-on-release, so the state machine lives in `input.cpp`).
- UI rendering: per-zone dirty-flags byte (`DIRTY_HEADER | DIRTY_FREQ | DIRTY_RDS | DIRTY_METER | DIRTY_VOL | DIRTY_FOOTER`) → partial `fillRect` + `drawString` on only the affected zone. The analog S-meter needle renders through a dedicated `TFT_eSprite` for flicker-free animation between radio polls. Modal menu is a full-screen takeover — input routing and paint skip the dirty-flag pipeline while `menuIsOpen()` is true.
- Frequency units are **band-mode-dependent**: FM in 10 kHz library units, AM/MW/SW in 1 kHz units. Callers should not hard-code FM's convention — use `radioFormatFrequency()` for display strings and `radioGetCurrentBand()->step` for encoder deltas.
- **Threading model (dual-core):** a dedicated FreeRTOS task `radioTask` (in `radio.cpp`) is pinned to **Core 0** and drives Si4735 I²C polling on its own cadence (signal 500 ms, RDS 200 ms). The Arduino `loopTask` stays on **Core 1** (default) and owns the UI + input + persistence. All radio.cpp functions are thread-safe: each takes an internal `SemaphoreHandle_t` mutex for the duration of its library call or cached-state access. `radioPollSignal()` / `radioPollRds()` no longer kick I²C themselves — they drain "has the cached value changed" flags set by the task. `radioSetBand()` / `radioSetFrequency()` / `radioSetVolume()` may briefly block (≤ ~20 ms) if they collide with the task mid-poll. RDS getters are **copy-into-buffer** (`radioGetRdsPs(buf, size)` / `radioGetRdsRt(buf, size)`) — the pointer-returning form was retired because strings racing across cores is unsafe. RadioText is sanitised at source (ATS-Mini triple-gate + whitespace/control-char scrub) so callers only ever see printable text or an empty string; the old UI-layer printable guard has been removed.
- **RDS gate (user setting):** `radioSetRdsEnabled(false)` makes `pollRdsLocked()` early-return before any I²C traffic, pushes `setRdsConfig(0, …)` to the chip so its internal decoder stops, and clears the PS / RT / PI mirrors so the UI falls back to the band scale. Re-enabling resets the 200 ms poll throttle so the next task tick fetches fresh status. The flag is persisted in NVS (`rds_en`, default 1) and restored in `setup()` after `radioInit()`.
- **Header status icons:** the top zone renders three indicator widgets (`drawBleIndicator`, `drawWiFiIndicator`, `drawRdsIndicator`) left of the band tag and battery. Each is hidden when its feature is off, drawn dim in `TH.rf_icon` when enabled, and bright in `TH.rf_icon_conn` when connected — for RDS that means "chip reports in-sync"; for Bluetooth / WiFi the `enabled` state means the controller / modem is physically up (see `connectivity.cpp`), and the `connected` state is reserved for future PRs that wire up the real BLE GATT / WiFi STA connect flow. Toggled from the **Settings** submenu.
- **BT / WiFi boot order:** `connectivityEarlyInit()` is called right after `Serial.begin()` as a named landmark, but it is a no-op — Arduino-ESP32 leaves both radios in their lowest-power state by default, so no early hardware poke is required. The real work happens after `persistInit()` via `connectivitySetBtEnabled()` / `connectivitySetWifiEnabled()`, which on fresh NVS (both flags default to 0) are also no-ops, and on subsequent boots bring the respective radio back up if the user had previously enabled it.
- **Do not call radio.cpp functions before `radioInit()` returns** — the mutex is created there. Conversely, `radioStart()` must be called exactly once after `radioInit()` to launch the task; before that, radio.cpp still works but polling doesn't happen automatically.
- `persist.cpp` coalesces writes with a ≥1 s per-key rate limit so rapid encoder rotation doesn't hammer flash. Schema-version mismatch on boot wipes the namespace and resets to defaults. Current schema is **v6**, which adds 16 `preset<N>` u32 slots (all empty by default; bit layout in [include/preset_pack.h](include/preset_pack.h)) on top of v5's `bl_level`, v4's `rds_en` / `bt_en` / `wifi_en` and v3's BW/AGC per-mode shadows; upgrades from v1 / v2 / v3 / v4 / v5 are additive and preserve every pre-existing key.
- **Memory presets (user setting):** 16 slots saved to NVS under `preset0`..`preset15` as packed u32 (`valid | band | freq` — see [include/preset_pack.h](include/preset_pack.h) for the bit layout). Slots store `band + freq` only; BW / AGC per-mode state is *not* slot-scoped on purpose so loading a slot doesn't silently overwrite the user's preferred bandwidth for the whole mode. Save / Load / Delete are all menu-driven from `MENU_MEMORY_SLOT`. Saves are immediate (no rate-limit coalescing) since a single menu click can't hammer flash. The Memory submenu renders each slot via `labelMemory()` which reuses `radioFormatFrequencyPure()` for the mode-aware frequency string.
- **Bottom touch-button row (transport-style, 5 buttons):** `⏪ ◀ 🔊 ▶ ⏩` at `y=180..230` — Seek Down / Prev Preset / Mute / Next Preset / Seek Up. Drawn by `drawButtonRow()` in [src/Draw.cpp](src/Draw.cpp) from procedural triangles + a hand-drawn speaker (no bitmap fonts). Hit-tested in `handleTouch()` in [src/main.cpp](src/main.cpp) against the `BTN_*_X` / `BTN_W` constants in [include/Draw.h](include/Draw.h) (single source of truth so drawn geometry and hit rects can't drift). Hidden while a bandscope scan runs — the scan graph extends past the button-row Y. Preset-nav buttons call `persistFindPresetFreq(band, current, dir)` in [src/persist.cpp](src/persist.cpp) which filters to the current band and navigates by freq (not slot index), wrapping at the ends; a tap with no presets saved for this band is a silent no-op. `seekIsActive()` gates the Core-0 poll loop through the same `radioScanEnter/Exit` handshake Scan uses, so seek and scan are mutually exclusive. Seek includes a peak-climb so adjacent-channel RSSI bleed doesn't stop us 100 kHz off the true carrier. Mute is a latched flag in `radio.cpp` (`radioSetMute` / `radioGetMute`); `radioScanExit` restores audio to `g_userMute` rather than unconditional false, and `applyBandLocked` re-asserts it so a band switch can't silently un-mute. Not persisted on purpose — mute on reboot would be user-hostile. Touch coords come through a rotation-0-cal → rotation-3-runtime remap (`landscape_x = 319 - 4·rawY/3`, `landscape_y = 3·rawX/4`) that fixes the prior transform's underflow for taps below y≈170.
- TFT touch uses a hard-coded XPT2046 calibration (`{ 477, 3203, 487, 3356, 6 }`) lifted from `src/test_shield.cpp`. Re-run `calibrateTouch()` there if the shield is replaced.

## Code Style

- C++ with Arduino conventions (`setup`/`loop`, `constexpr` for constants, `static` file-scope helpers).
- Code organized into numbered sections with banner comments.
- All pin assignments and tuning parameters are `constexpr` constants at the top of `main.cpp`.
- Functions have Doxygen-style or concise inline doc comments.
- All comments and documentation in English.

## Workflow

- **Branching:** every task begins on a new branch off `master` (`feature/<short-name>` or `fix/<short-name>`, e.g. `git checkout -b feature/add-unit-tests`). Never commit directly to `master`. Merge via PR once tests and the `esp32dev` firmware build pass.

## Versioning & Releases

- **Scheme:** Semantic Versioning (`vMAJOR.MINOR.PATCH`). Git tags on GitHub are the single source of truth for version numbers.
- **Version injection:** `scripts/version.py` is a PlatformIO pre-build script (registered once via `extra_scripts = pre:scripts/version.py` in the `[common]` section, so every env inherits it) that writes `include/version.h` from `git describe --tags --dirty`. The generated file is listed in `.gitignore` — **never commit it, never edit it by hand**.
- **Macros available in code:** `FW_VERSION`, `FW_GIT_COMMIT`, `FW_GIT_DIRTY`, `FW_BUILD_DATE`. Consumed by the `FW_IDENTITY` string in `src/main.cpp`, which is kept alive by a no-op inline asm reference in `setup()` (the `used` attribute alone is not enough against `--gc-sections`). Visible via `strings .pio/build/esp32dev/firmware.elf | grep FW=`. `FW_VERSION` is deliberately rendered in the header (top-right) and footer so the running build is identifiable without a serial monitor.
- **Cutting a release** (full checklist in [docs/releasing.md](docs/releasing.md)):
  1. Move `[Unreleased]` items in `CHANGELOG.md` into a new `[X.Y.Z] - YYYY-MM-DD` section; update compare links.
  2. Commit, then `git tag -a vX.Y.Z -m "Release vX.Y.Z"`.
  3. `git push origin master && git push origin vX.Y.Z` — pushing the tag triggers `.github/workflows/release.yml`, which builds the `esp32dev` firmware and publishes a GitHub Release with `digi_radio-vX.Y.Z-esp32dev.{bin,elf}` attached.
  4. After the workflow publishes the Release, replace GitHub's auto-generated "What's Changed" stub body with the matching CHANGELOG section (same command as `docs/releasing.md` step 7):
     ```bash
     gh release edit vX.Y.Z --notes-file <(awk '/^## \[X\.Y\.Z\]/{p=1; print; next} /^## \[/{p=0} p' CHANGELOG.md)
     ```
     Skipping this leaves the Release body as a bare PR-title + compare link; `CHANGELOG.md` is the canonical narrative of what shipped and must land on the Releases page too, not only in the repo.
- **Do not** bump versions by editing files — the only knob is `git tag`.
- **Do not** build `shield_test` env in the release workflow — it is a debug fixture for the Waveshare TFT shield, not a product artifact.
- **Do not** rewrite or force-push a tag that has already been published as a Release — cut a new patch version instead.
- **Do not** re-introduce the OLED variant. The 128×64 SSD1315 build has been retired; any future display variant should live on a parallel branch rather than branching back off `src/main.cpp`.
