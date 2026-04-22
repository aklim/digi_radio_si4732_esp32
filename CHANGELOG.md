# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.8.0] - 2026-04-22

### Added

- **RDS Clock-Time in sidebar.** The `Time: --:--` placeholder reserved in
  the info box since v2.1 now shows real station time from RDS group 4A.
  The Core-0 task decodes each fresh CT frame via PU2CLR's
  `getRdsDateTime(year, month, day, hour, minute)` overload — the library
  applies the station's Local Time Offset internally, so the displayed
  value is local time at the transmitter (not UTC). Falls back to
  `--:--` on non-FM bands, when RDS sync is stale (≥ 10 s), when the
  user has turned RDS off in **Settings**, or before the first CT group
  decodes. No NVS schema bump — CT rides the existing `rds_en` gate
  and `RDS_STALE_MS` stale clock alongside PS / RT / PI.

  Observation from live Ukrainian FM traffic: PU2CLR's decoder lands
  year in 1906/1907 on most transmitters due to an MJD underflow after
  LTO correction, even though HH:MM is correct. We therefore gate the
  CT display on `rdsCtHmIsValid(h, mi)` and ignore the date components
  entirely; a date row is a future enhancement that would bring its
  own validator + mirrors.

  **Known limitation — RDS CT quirk.** The clock value is only as
  accurate as the broadcaster makes it. Many commercial and regional
  stations keep their CT aligned to UTC without the correct LTO, or
  never update for DST, so different stations may show different times
  that all disagree with the listener's wall clock. This is a
  standards-compliant RDS behaviour, not a firmware bug — other
  receivers (including ATS-Mini) show the same discrepancies on the
  same transmitters. International public broadcasters (BBC, DLF,
  Kan, RAI) and well-run national networks (Suspilne in Ukraine) are
  usually correct. A future release may add a WiFi + NTP time source
  with RDS CT as an optional overlay; see
  [docs/future_improvements.md](docs/future_improvements.md).
- **`rdsCtHmIsValid` / `rdsCtFormatHM` pure helpers.** New
  [include/rds_ct.h](include/rds_ct.h) + [src/rds_ct.cpp](src/rds_ct.cpp)
  module (no Arduino / SI4735 / FreeRTOS deps) that range-checks the
  HH:MM components and hand-assembles zero-padded `HH:MM` without
  pulling `snprintf`'s stdio float formatter. Shared between the
  firmware and the native host tests so the same gate runs in both.
- **`radioGetRdsCt` / `radioGetRdsCtValid` public API.** Copy-into-buffer
  getter by direct analogy with `radioGetRdsPs` / `radioGetRdsRt`;
  guarded by the same mutex. Added to [include/radio.h](include/radio.h)
  next to `radioGetRdsPi`.
- **`test_native_ct` Unity suite.** 14 host-side tests covering HH:MM
  range validation, zero-padding, overflow-clamp, buffer-too-small
  safety, and NULL-buffer safety. Runs under the existing
  `pio test -e native` (~2 s) and the same CI gate in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).

### Changed

- `radioSetRdsEnabled(false)` now also clears the CT mirror (hour /
  minute / valid flag) along with PS / RT / PI so a later re-enable
  can't briefly flash stale time before the next decode arrives.
- `drawInfo()` in [src/Draw.cpp](src/Draw.cpp) replaces the hardcoded
  `"--:--"` on the Time row with the CT mirror value when a valid CT
  frame has decoded on an FM band; the placeholder string is retained
  only as the fallback for the no-CT cases enumerated above.

## [2.7.0] - 2026-04-22

### Added

- **Transport-style touch-button row.** Five fingertip-sized buttons
  (58×50 px each, 5 px gaps) at `y=180..230` below the band scale:
  `⏪  ◀  🔊  ▶  ⏩` — Seek Down / Prev Preset / Mute / Next Preset
  / Seek Up. All icons drawn procedurally in
  [src/Draw.cpp](src/Draw.cpp) (no bitmap fonts), so the label-vs-arrow
  overlap from the prototype goes away and the row reads from across
  the bench.
- **Auto-seek.** Outer double-arrow buttons step the current band at
  `band->step` until a frequency meets per-mode RSSI + SNR thresholds
  (`RSSI≥15 / SNR≥8` on FM, `RSSI≥25 / SNR≥8` on AM/SW) or wrap the
  full band with no hit (origin restored). Includes a peak-climb so
  adjacent-channel RSSI bleed doesn't land us 100 kHz off the true
  carrier — once a step clears the threshold, the engine keeps probing
  up to 3 further steps while RSSI keeps rising and lands on the local
  maximum. Implemented as a tick-driven state machine in
  [src/Seek.cpp](src/Seek.cpp) on top of the existing
  `radioScanEnter / radioScanMeasure / radioScanExit` primitives, so
  the Core-0 poll loop is paused for the duration and audio is muted
  through each step. Any encoder or touch input aborts.
- **Preset navigation.** Inner single-arrow buttons jump through
  memory-preset slots saved for the current band, in order of frequency
  (not slot index). Wraps at the ends — pressing Next at the highest
  preset cycles back to the lowest. New
  `persistFindPresetFreq(band, currentFreq, dir)` helper in
  [src/persist.cpp](src/persist.cpp) iterates the 16-slot table once
  and returns 0 when no presets exist for the current band (the tap
  becomes a silent no-op).
- **Mute toggle.** Centre speaker-icon button flips a new user-latched
  mute in [src/radio.cpp](src/radio.cpp) (`radioSetMute` /
  `radioGetMute`). The latch survives band switches (re-asserted in
  `applyBandLocked`) and bandscope scans (`radioScanExit` restores it
  instead of unconditionally un-muting). Not persisted on purpose —
  mute on reboot would be user-hostile. Muted state shows an inverted
  button fill *and* a slash overlay across the speaker glyph for
  at-a-glance legibility.
- **Touch transform fix.** The original rotation-0 calibration was
  being applied in rotation-3 runtime, which stretched X and compressed
  Y, making taps below `y≈170` fall outside every hit rect. Replaced
  the broken `ty = PANEL_W_NATIVE - 1 - rawX` transform with a proper
  rotation-0-cal → rotation-3-runtime remap (`landscape_x = 319 - 4·rawY/3`,
  `landscape_y = 3·rawX/4`) in
  [src/main.cpp](src/main.cpp) `handleTouch()`. The existing FREQ-zone
  tap that "worked" before was passing by coincidence; now every tap
  lands on its true screen position.
- **New `btn_*` theme fields.** Three fields (`btn_bg` / `btn_fg` /
  `btn_active`) added to `ColorTheme` in
  [include/Themes.h](include/Themes.h) and seeded for every one of the
  9 bundled palettes so the button row is legible in each.
- **`test_native_seek` Unity suite.** 10 host-side tests for the
  seek-step wrap arithmetic (`seekNextFreq` in
  [include/seek_step.h](include/seek_step.h)) — up/down/at-edge/wrap
  for both FM and AM/MW step granularities. Runs under the existing
  `pio test -e native` (~1 s) and the same CI gate in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).

## [2.6.0] - 2026-04-21

### Added

- **Memory presets.** Long-press → **Memory** opens a 16-slot list where each
  row shows the saved band + frequency ("01  102.4 MHz FM") or `<empty>`.
  Clicking a slot descends into a per-slot action submenu with
  `Load / Save current / Delete / Back`: **Save** snapshots the live
  `band + freq` into the slot and stays in the submenu so the row label
  refreshes immediately; **Delete** clears the slot and returns to the
  list; **Load** tunes the radio to the saved station and closes the menu
  to get out of the way. Slots store only `band + freq` on purpose —
  BW/AGC stay per-mode (current behaviour) so loading a preset doesn't
  silently overwrite the user's preferred filter for the whole mode.
- **New pure helper `include/preset_pack.h`.** Packs a `PresetSlot`
  (`valid / band / freq`) into a single `uint32_t` for NVS storage (bit
  layout: valid=MSB, band=bits 19..16, freq=low 16 bits, bits 30..20
  reserved for future per-slot metadata without a schema bump). Shared
  between `persist.cpp` and the new native test suite.
- **Persist schema v6.** 16 new `u32` keys — `preset0`..`preset15`, all
  default `0` (= empty slot). Upgraders from v1 / v2 / v3 / v4 / v5 keep
  their existing state and have the 16 preset keys seeded to empty;
  wipe path seeds them too.
- **`test_native_preset` Unity suite.** 12 host-side tests covering the
  pack/unpack round-trip, bit-layout invariants, empty-slot encoding,
  overflow masking (band >= 16), and forward-compat handling of the
  reserved bits. Runs under the existing `pio test -e native` (~7 s total
  across all suites) and the same CI gate in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).

## [2.5.0] - 2026-04-21

### Added

- **Backlight brightness picker.** Long-press → **Settings → Brightness**
  opens a 5-step picker (20 / 40 / 60 / 80 / 100 %) with the active level
  marked. The choice is applied immediately via the LEDC PWM on `TFT_BL`
  and persisted to NVS so it survives reboots. New `backlight.cpp` /
  `backlight.h` module owns the LEDC channel + percent→duty mapping.

- **Persist schema v5.** One new `u8` key — `bl_level` (default
  `BACKLIGHT_DEFAULT_PERCENT` = 55). Upgraders from v1 / v2 / v3 / v4
  keep their existing state and get the default brightness seeded;
  wipe path seeds the default too.

### Changed

- **Power reduction.** Three targeted changes land together:
  - `setCpuFrequencyMhz(80)` on boot. The firmware has no DSP, no I2S
    audio, and no CPU-bound workload — 80 MHz is the documented
    minimum for the ESP32 WiFi/BT stacks and is more than enough for
    Si4732 I²C (100 kHz) + hardware-SPI-driven TFT_eSPI. Logged early
    in `setup()` so the applied frequency is visible in the serial
    console.
  - `vTaskDelay(1)` at the end of `loop()`. The Arduino `loopTask`
    runs at priority 1 above the FreeRTOS idle task (priority 0), so
    without an explicit yield Core 1 spins at full clock even when
    every early-out in the loop fires. A single-tick yield per
    iteration lets the scheduler enter the idle-task sleep path.
  - Default TFT backlight level dropped from ~86 % (220/255) to 55 %
    (the new persisted default). The backlight LED is the largest
    single continuous consumer after the Si4732 itself; 55 % remains
    clearly visible in a lit room. Users who want more can raise it
    via the new Brightness picker.

## [2.4.0] - 2026-04-21

### Changed

- **Bluetooth and WiFi toggles are now real.** Flipping Bluetooth or
  WiFi off in the Settings submenu physically (de)initializes the
  ESP32 radios via `connectivity.cpp` instead of just hiding a header
  icon — BT gets `btStop()` + `esp_bt_controller_disable()` +
  `esp_bt_controller_deinit()`, WiFi gets `WiFi.mode(WIFI_OFF)`. Both
  teardown paths no-op when the respective radio is already in its
  idle/OFF default state, which is what Arduino-ESP32 boots into —
  so fresh-boot power draw is unchanged and there is no one-way
  `esp_bt_mem_release` call anywhere. Both flags default to off on
  first boot for power saving. GATT / STA connect remain reserved for
  a future PR, so `getBleStatus()` / `getWiFiStatus()` still only
  return 0 or 1 and the header icons stay in the dim "enabled, not
  connected" variant when on.

## [2.3.0] - 2026-04-21

### Added

- **Header status icons.** Three compact indicators now render in the top
  icon row — an RDS pictogram (two interlocking concentric-ring "stereo
  swirls" drawn procedurally via `drawSmoothCircle` + `fillCircle`), the
  Bluetooth rune (ATS-Mini 5-line glyph), and the WiFi fan (3 concentric
  arcs). Each is hidden when its feature is off, drawn dim (`TH.rf_icon`)
  when enabled, and drawn bright (`TH.rf_icon_conn`) on a live
  connection — for RDS that means "chip reports sync"; for BT/WiFi the
  "connected" state is reserved for a future PR that wires up the real
  radio stacks.
- **Settings submenu.** Long-press → **Settings** opens a new submenu
  with on/off toggles for RDS, Bluetooth, and WiFi. Unlike the Band /
  BW / AGC pickers, Settings stays open after a click so multiple
  toggles can be flipped in one visit; `< Back` returns to the top.
- **Persist schema v4.** Three new `u8` keys — `rds_en` (default 1),
  `bt_en` (default 0), `wifi_en` (default 0). Upgraders from v1 / v2 /
  v3 keep their existing state; wipe path seeds defaults.
- **connectivity.cpp / .h.** New module that tracks the user's
  Bluetooth / WiFi preference and exposes ATS-Mini-signature
  `getBleStatus()` / `getWiFiStatus()` for the header widgets. The
  real BLE / WiFi stacks are not started in this release — the module
  is a scaffold so follow-up PRs can drop a full stack in without
  touching Draw / Layout code.

### Changed

- **RDS decode is now gateable.** When the user turns RDS off in
  Settings the Core-0 task's RDS poll short-circuits before any I²C
  traffic, `setRdsConfig(0, …)` disables the chip's decoder, and the
  PS / RT / PI mirrors are cleared so the RDS zone falls back to the
  band scale. Re-enabling resumes normal operation within the next
  poll tick. Default remains "RDS on", matching prior firmware.
- **Layout-Default paint order.** S-meter and stereo indicator now
  render *before* the BLE / WiFi / RDS header icons so the S-meter's
  `fillRect(0, 0, 211, 16, TH.bg)` clear doesn't erase icons sitting
  inside the strip. Icons land in the free slots past the 83-px bar
  extent or to the right of the S-meter band.

## [2.2.2] - 2026-04-19

### Added

- **Native Unity test suite.** Pure helpers that previously had no safety
  net — `radioFormatFrequencyPure()`, `rdsSanitizeRt()`, and the
  `g_bands[]` table — are now covered by 34 host-side unit tests split
  across three suites under `test/test_native_*`. Runs via
  `pio test -e native` in ~2 s with no ESP32 hardware required.
- **CI gate.** [.github/workflows/ci.yml](.github/workflows/ci.yml) runs
  the native test suite on every push and pull request targeting
  `master`, blocking regressions before merge. The release workflow is
  unchanged — it still fires only on `vX.Y.Z` tags.

### Changed

- **`radio.cpp` split for testability.** Band table (`Band` struct +
  enums in [include/radio_bands.h](include/radio_bands.h),
  definitions in `src/band_table.cpp`), RDS RadioText sanitiser
  ([include/rds_sanitize.h](include/rds_sanitize.h) +
  `src/rds_sanitize.cpp`), and the pure frequency formatter
  ([include/radio_format.h](include/radio_format.h)) moved out of
  `src/radio.cpp` so the native test env can link them without pulling
  Arduino / SI4735 / TFT_eSPI / FreeRTOS. No runtime behaviour change on
  hardware — `radio.h` still re-exports the same names, and
  `radioFormatFrequency()` delegates to the pure helper under the
  mutex.

## [2.2.1] - 2026-04-19

### Added

- **Encoder-mode highlight.** Short-click toggles FREQUENCY ↔ VOLUME but
  previously had no on-screen feedback, so the user could not tell which
  parameter the next rotation would change. The sidebar `Vol: NN` row now
  renders in `FreeSansBold9pt7b` while VOLUME is active; absence of the
  highlight implicitly means FREQUENCY. Bitmap Font 2 has no bold sibling
  in TFT_eSPI — switching to a GFX free font is the canonical way; both
  fonts share `ML_DATUM` vertical centring, so the row Y does not shift
  when the highlight toggles.

## [2.2.0] - 2026-04-18

### Added

- **BW menu entry.** Pick the Si4732 IF filter from the active band's
  catalogue (FM: `Auto / 110k / 84k / 60k / 40k`; AM/SW: `1.0k .. 6.0k`,
  seven steps). The FM and AM filter indices are tracked independently so
  each mode keeps its own choice across band switches.
- **AGC menu entry.** Toggle AGC on, disable it with no attenuation, or
  select a manual attenuator step. FM allows indices 0..27 (1 + 27 att),
  AM allows 0..37 (1 + 37 att). Index semantics match ATS-Mini
  (`0` → AGC on, `≥1` → AGC off + att `idx-1`).
- **Scan scroll.** After a bandscope sweep completes, the rotary encoder
  tunes normally while the graph is held — the waveform re-centers on the
  live frequency as you rotate, mirroring ATS-Mini's `CMD_SCAN` branch.
  Click exits the scan at the current tune (saved to NVS); long-press
  aborts and restores the pre-scan frequency. During the sweep itself
  rotation is still ignored to avoid corrupting in-flight samples.
- **Menu viewport scrolling.** List pickers now scroll the visible window
  to keep the cursor on-screen, so the 28–38 AGC rows and future long
  lists render without overflowing into the hint footer.

### Changed

- BW and AGC are re-applied in `applyBandLocked()` so user selections
  survive band switches. Previously the chip reverted to each mode's
  power-on default on every `setFM` / `setAM`.
- **AGC indexing realigned with ATS-Mini.** Index `1` now means "AGC off,
  no attenuation" (ATS-Mini `doAgc`), and `idx = N` maps to AGCIDX = `N-1`.
  Pre-existing attenuator selections are interpreted one step further out
  after the upgrade — this only affects users who had set a manual
  attenuator by editing the firmware before this release.
- `CMD_SCAN` renumbered from `0x1200` to `0x1B00`; `CMD_AGC = 0x1200` and
  `CMD_BANDWIDTH = 0x1300` take their ATS-Mini-matched slots. Menu
  command codes are internal enums and never persisted, so no migration
  is needed.

### Fixed

- **S-meter length and stereo-split.** `strengthFromRssi` now uses
  ATS-Mini's mode-dependent S-point table (FM / AM thresholds,
  Utils.cpp `getStrength`) returning 1..17 instead of the previous
  linear 0..49 stub. The meter bar stops at the right length and the
  stereo-indicator stripe covers the whole active bar cleanly.
- **Battery icon geometry.** `drawBattery` now matches ATS-Mini 1:1:
  `drawRoundRect` outline (no AA halo), stepped positive terminal via
  two `drawLine` calls at x+29 / x+30, and `fillRoundRect(r=2)` inner
  level so the fill stays inside the contour's rounded corners. Fixes
  the "cut-out" artefact and the fill bleeding through the frame.
- **Battery icon + voltage label no longer clipped by the band tag.**
  `drawBandAndMode` was doing a per-widget `fillRect(x - 80, y - 1, 220, 30)`
  which, with `BAND_OFFSET_X=150`, wiped x=70..290 every frame — eating
  the left 6 px of the battery frame and the entire voltage string
  (text sits at x≈251..281). Removed the clear; ATS-Mini's implementation
  doesn't do it either and the fullscreen sprite clear already handles
  inter-frame erase.
- **Random characters under the frequency when RT is empty.**
  `drawRadioText` walks the RT buffer line-by-line via `rt += strlen(rt)+1`
  (ATS-Mini's multi-line convention with double-NUL termination). Our
  `pollRdsLocked` sanitizer was writing a single-line result into an
  uninitialised stack buffer, then `memcpy(g_rt, cleaned, 65)` copied
  65 bytes including the garbage past the first NUL. The multi-line
  walk stepped into that garbage and rendered it where the band scale
  should have been. Fixed by zero-initialising the sanitizer stage
  buffer so every byte past the terminator is also NUL.
- **Sidebar bottom no longer clipped by the scale.** `drawScale`'s
  per-widget `fillRect(0, 120, 320, 50)` was overwriting the sidebar's
  bottom border (sidebar ends at y=129, the clear started at y=120).
  Removed — matches ATS-Mini (the full sprite is cleared each frame in
  `updateDisplay`, so the per-widget clear was redundant).
- **Blank RadioText flashes eliminated at the source.** `pollRdsLocked()`
  now gates on `getRdsSyncFound()` (matching ATS-Mini's triple-gate in
  `Station.cpp`) and sanitises the PU2CLR RT buffer before publishing —
  leading whitespace is stripped, non-printable bytes are folded to
  spaces, trailing whitespace is trimmed, and an all-empty result is
  rejected. The UI-level workaround in `Layout-Default.cpp` has been
  removed; if `radioGetRdsRt()` returns non-empty, the contents are
  guaranteed to be real printable text.

### Persist

- Schema `v2 → v3` adds four keys: `bw_fm`, `bw_am` (IF-filter index per
  mode) and `agc_fm`, `agc_am` (AGC/attenuator index per mode). Defaults:
  `bw_fm = 0` (Auto), `bw_am = 4` (3.0 kHz), `agc_fm = 0` (AGC on),
  `agc_am = 0` (AGC on). The `v1 → v3` and `v2 → v3` upgrade paths
  preserve existing `band` / `vol` / `freq<N>` / `theme` values.

## [2.1.0] - 2026-04-18

### Changed

- **UI is now a 1:1 port of [ATS-Mini](https://github.com/esp32-si4732/ats-mini).**
  The panel runs in landscape (`setRotation(3)` → 320×240) and the widget
  layout, fonts, and colour-theme struct mirror upstream. New files:
  `Draw.{h,cpp}`, `Layout-Default.cpp`, `Scan.{h,cpp}`, `Themes.{h,cpp}`,
  `Battery.{h,cpp}` — each corresponds directly to an `ats-mini/*` file of
  the same name. See [docs/display_tft.md](docs/display_tft.md).
- **Fonts:** big frequency now renders in TFT_eSPI Font 7 (7-segment
  digital-clock face, the same one ATS-Mini calls via `drawFloat(..., 7)`);
  band tag + mode box use Orbitron_Light_24.
- **Themes:** the legacy `COL_*` constants retire; 9 ATS-Mini presets
  (Default / Bluesky / eInk / Pager / Orange / Night / Phosphor / Space /
  Magenta) selectable from the `Theme` menu entry and persisted across reboots.
- **Sidebar:** rounded info box at (0, 18) with live Step / BW / AGC /
  Vol / PI and a placeholder Time row.
- **Signal stack:** top-edge segmented S-meter with stereo indicator slit
  replaces the previous needle gauge; band scale with tick marks occupies
  the bottom zone.

### Added

- **Bandscope sweep (`Scan` menu entry).** Silences audio, sweeps 200
  frequency points around the current tune, and plots RSSI (green) and
  SNR (yellow) lines over a dotted grid. Matches ATS-Mini's CMD_SCAN
  behaviour byte-for-byte; click or long-press to abort and return to
  the listener's original frequency.
- **Radio API:** `radioGetRdsPi`, `radioGet/SetBandwidthIdx`,
  `radioGet/SetAgcAttIdx`, plus low-level `radioScanEnter` /
  `radioScanMeasure` / `radioScanExit` hooks for Scan.cpp. Core-0 polling
  task silences itself while a sweep is active.
- **Persist schema v2**: adds a `theme` slot. v1 stores lazy-upgrade in
  place (band / volume / per-band freq preserved).

### Fixed

- Full-screen UI repaints no longer flicker: renders go through an 8-bit
  full-screen `TFT_eSprite` (76 KB heap) with atomic `pushSprite`, same
  pattern ATS-Mini uses for its 16-bit 320×170 buffer.

## [2.0.0] - 2026-04-18

Release marking the architectural rewrite of the firmware: the 128×64
OLED variant is retired, the TFT becomes the sole product, the radio
code is now multi-band (FM / MW / SW) and runs Si4732 polling on a
dedicated Core 0 task. Long-press menu + NVS persistence land too.

First big step on the road to
[ATS-Mini](https://github.com/esp32-si4732/ats-mini) feature / UI
parity. The remaining roadmap (SSB / BFO, memory presets, seek,
waterfall, themes, settings menu, LW + more SW bands) is tracked in
[docs/future_improvements.md](docs/future_improvements.md).

**Breaking for anyone running `v1.1.0`**: the OLED artifact is gone
(there is no more `esp32dev` OLED build) and the shared `radio.cpp` /
`input.cpp` API signatures changed. The `esp32dev` env name now refers
to the TFT firmware; upgrade to the TFT shield or stay on
[`v1.1.0`](https://github.com/aklim/digi_radio_si4732_esp32/releases/tag/v1.1.0).

### Added
- **Dual-core radio task.** Si4732 signal (500 ms) and RDS (200 ms)
  polling moved to a dedicated FreeRTOS task pinned to Core 0, leaving
  the Arduino `loopTask` on Core 1 to own UI + input + persistence
  without I²C-induced stalls. A single `SemaphoreHandle_t` mutex inside
  `radio.cpp` serialises every Si4735 library call, so `radioSetBand`
  / `radioSetFrequency` / `radioSetVolume` from the UI coexist safely
  with the task's polling. New `radioStart()` entrypoint launches the
  task; `setup()` calls it after `radioInit()` returns. Pattern drawn
  from H.-J. Berndt's
  [pocketSI4735DualCoreDecoder](http://www.hjberndt.de/dvb/pocketSI4735DualCoreDecoder.html).
  (docs/future_improvements.md → Performance / architecture → dual-core)

- TFT variant: **multi-band receiver**. New `Band` table in `radio.cpp`
  with four bands — FM Broadcast (87.0–108.0 MHz), MW (520–1710 kHz),
  SW 41 m (7100–7300 kHz), SW 31 m (9400–9900 kHz). `radioSetBand(idx)`
  retunes the Si4735 via `setFM` / `setAM`, re-applies volume, and
  restores the band's last-used frequency. RDS / stereo polling is
  gated to FM. Header label and footer unit switch automatically
  ("102.4 MHz" ↔ "1530 kHz") via the new mode-aware
  `radioFormatFrequency()`. (docs/future_improvements.md → Radio features → AM/SW/LW bands)
- TFT variant: **encoder long-press menu**. Holding the encoder button
  for ≥500 ms opens a full-screen modal (`src/menu.cpp`) with entries
  `Band → [FM Broadcast / MW / SW 41m / SW 31m]` and `Close`. Encoder
  rotates the cursor, click confirms. Reuses the GFX free-font pipeline
  from the needle/fonts PR. `ButtonEvent { BTN_NONE, BTN_CLICK,
  BTN_LONG_PRESS }` discriminates clicks from long presses, inspired by
  ATS-Mini's `ButtonTracker`. (docs/future_improvements.md → Input → menu system)
- TFT variant: **NVS persistence** via new `src/persist.cpp` wrapper
  around `<Preferences.h>`. Stores current band, per-band last-tuned
  frequency, and volume; schema-versioned (`PERSIST_SCHEMA_VER`) à la
  ATS-Mini so future key changes can wipe cleanly. Writes are
  rate-limited (≥1 s per key) so rapid encoder rotation doesn't
  hammer flash. First boot loads defaults from the band table.
- TFT variant: analog needle S-meter. The flat RSSI bar is replaced by a
  sprite-backed arc gauge with tick marks at every 10 dBµV and a
  green→yellow→red needle that animates smoothly (EMA-smoothed,
  ~30 Hz redraw) between RSSI samples. Dial chrome renders through a
  `TFT_eSprite` to stay flicker-free.
- TFT variant: Adafruit-GFX FreeFonts throughout the UI
  (`FreeSansBold24pt7b` for the frequency readout, `FreeSansBold12pt7b`
  for section headers, `FreeSans9pt7b` / `FreeSansBold9pt7b` for labels
  and numeric values). Replaces the legacy bitmap fonts (FONT2/4/7).
  Enabled via `LOAD_GFXFF` in `include/User_Setup.h`.

### Changed
- `radio.h`: RDS getters are now **copy-into-buffer**
  (`radioGetRdsPs(char*, size_t)` / `radioGetRdsRt(char*, size_t)`).
  The previous `const char*` returning form was unsafe once the radio
  task started writing the mirror buffers on Core 0. Affects callers
  in `main.cpp::drawRds`.
- `radio.h`: `radioPollSignal()` / `radioPollRds()` now drain
  change-flags set by the task — they no longer kick I²C themselves.
  Semantics from the caller's perspective are unchanged (returns true
  when the cached value moved since the last drain).
- `encoderPollButton()` returns a `ButtonEvent` enum
  (`BTN_NONE` / `BTN_CLICK` / `BTN_LONG_PRESS`) instead of `bool`.
- `radioSetFrequency()` clamps to the current band's min/max and
  updates its `Band::currentFreq` field so band-switch-round-trip
  preserves tune.

### Removed
- **OLED variant retired.** The legacy 128×64 SSD1315 firmware
  (`src/main.cpp` + Adafruit SSD1306 / GFX dependencies) has been
  deleted. All development now targets the Waveshare 2.8" ST7789V TFT
  shield. The `esp32dev_tft` PlatformIO env + `main_tft.cpp` /
  `ui_layout_tft.h` file names have been consolidated into a single
  `esp32dev` env, `src/main.cpp`, and `include/ui_layout.h` — the
  `_tft` suffix served to disambiguate variants that no longer exist.
  The release workflow publishes a single `digi_radio-vX.Y.Z-esp32dev.{bin,elf}`
  artifact per tag.

### Notes
- The new frequency font is anti-aliased but smaller than the legacy
  7-segment FONT7. If the bigger 7-seg look is missed, a follow-up can
  vendor the open-source `DSEG7_Classic_Bold_48` GFX font under
  `include/fonts/` and swap the `FREQ_FONT` constant in `src/main_tft.cpp`
  — no other changes needed.
- First step on the road to [ATS-Mini](https://atsmini.github.io/)
  feature/UI parity. See [docs/future_improvements.md](docs/future_improvements.md)
  for the remaining roadmap (menu system, AM/SW/LW bands, SSB, memory
  presets, waterfall, themes, …).

## [1.1.0] - 2026-04-18

### Added
- `esp32dev_tft` firmware variant driving the Waveshare 2.8" ST7789V TFT
  shield: 240×320 colour UI with large frequency, S-meter (0–60 dBµV
  with tick marks), RDS PS and RadioText, stereo pilot indicator, SNR,
  firmware-version footer, and a yellow focus border around the active
  zone. XPT2046 touch zones switch between frequency and volume
  adjustment as an alternative to the encoder button. See
  [docs/display_tft.md](docs/display_tft.md).
- `DISPLAY_FLIPPED` knob in `include/ui_layout_tft.h` rotates the TFT UI
  180° for enclosures that cannot physically flip the shield; touch
  coordinates are mirrored to match.
- Shared `radio.cpp` / `input.cpp` modules reused by both the OLED and
  TFT firmwares — the two mains differ only in the display backend and
  the UI layout.
- [docs/future_improvements.md](docs/future_improvements.md) — living
  roadmap of v2+ ideas.

### Changed
- Release workflow now publishes both `digi_radio-vX.Y.Z-esp32dev.{bin,elf}`
  and `digi_radio-vX.Y.Z-esp32dev_tft.{bin,elf}` per tag.
- `src/main.cpp` refactored to consume the shared radio / input modules
  (no behaviour change — same splash, same FREQ/VOL toggle, same RSSI bar).
- `platformio.ini` hoists shared settings into a `[common]` section and
  uses explicit `build_src_filter` per env so shared sources never leak
  between builds.

## [1.0.0] - 2026-04-18

### Added
- FM receiver firmware for ESP32 + Si4732 + SSD1315 OLED + rotary encoder
  (87.0–108.0 MHz, 100 kHz step, dual-mode frequency/volume control, RSSI bar).
- Waveshare 2.8" TFT Touch Shield Rev 2.1 bring-up test environment
  (`pio run -e shield_test`) using TFT_eSPI.
- Project versioning infrastructure:
  - `scripts/version.py` PlatformIO pre-build hook that generates
    `include/version.h` from `git describe --tags --dirty`.
  - `FW_IDENTITY` string embedded in the firmware binary (readable via
    `strings firmware.elf | grep '^FW='`).
  - CHANGELOG, release runbook (`docs/releasing.md`), and GitHub Actions
    workflow that publishes `firmware.bin` / `firmware.elf` to
    [GitHub Releases](https://github.com/aklim/digi_radio_si4732_esp32/releases)
    on every `vX.Y.Z` tag.

[Unreleased]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.8.0...HEAD
[2.8.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.7.0...v2.8.0
[2.7.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.5.0...v2.6.0
[2.5.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.4.0...v2.5.0
[2.4.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.3.0...v2.4.0
[2.3.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.2.2...v2.3.0
[2.2.2]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.2.1...v2.2.2
[2.2.1]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.2.0...v2.2.1
[2.2.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/releases/tag/v1.0.0
