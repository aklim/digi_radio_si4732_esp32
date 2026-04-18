# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/releases/tag/v1.0.0
