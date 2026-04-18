# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- TFT variant: analog needle S-meter. The flat RSSI bar is replaced by a
  sprite-backed arc gauge with tick marks at every 10 dBµV and a
  green→yellow→red needle that animates smoothly (EMA-smoothed,
  ~30 Hz redraw) between RSSI samples. Dial chrome renders through a
  `TFT_eSprite` to stay flicker-free. Only affects `esp32dev_tft`.
- TFT variant: Adafruit-GFX FreeFonts throughout the UI
  (`FreeSansBold24pt7b` for the frequency readout, `FreeSansBold12pt7b`
  for section headers, `FreeSans9pt7b` / `FreeSansBold9pt7b` for labels
  and numeric values). Replaces the legacy bitmap fonts (FONT2/4/7).
  Enabled via `LOAD_GFXFF` in `include/User_Setup.h`.

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

[Unreleased]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/releases/tag/v1.0.0
