# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/aklim/digi_radio_si4732_esp32/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/aklim/digi_radio_si4732_esp32/releases/tag/v1.0.0
