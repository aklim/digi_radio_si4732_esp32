# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fully functional FM radio receiver firmware using the Silicon Labs Si4732 chip with an ESP32 microcontroller. Two peer firmware variants ship from this repo: `esp32dev` (128×64 SSD1315 OLED, `src/main.cpp`) and `esp32dev_tft` (240×320 ST7789V TFT shield + XPT2046 touch, `src/main_tft.cpp`). The Si4732 driver and rotary-encoder glue are factored into shared `src/radio.cpp` + `src/input.cpp`; the two mains differ only in the display backend and the UI layout. Built with PlatformIO and the Arduino framework.

## Build Commands

```bash
pio run -e esp32dev                       # Build OLED firmware
pio run -e esp32dev -t upload             # Build + flash OLED firmware
pio run -e esp32dev_tft                   # Build TFT firmware
pio run -e esp32dev_tft -t upload         # Build + flash TFT firmware
pio run -e shield_test -t upload          # TFT shield bring-up fixture (not a product)
pio run -e esp32dev --target size         # Program size for a given env
```

## Architecture

- **Build system:** PlatformIO (`platformio.ini`) with a `[common]` base section extended by three envs: `esp32dev` (OLED), `esp32dev_tft` (TFT), `shield_test` (TFT bring-up fixture). Each env uses an explicit `build_src_filter` to control which `src/*.cpp` files it pulls in.
- **Framework:** Arduino.
- **Sources:**
  - `src/main.cpp` — OLED entry + OLED-specific drawing.
  - `src/main_tft.cpp` — TFT entry + TFT-specific drawing + touch handling.
  - `src/radio.cpp` + `include/radio.h` — shared Si4732 wrapper (init, tune, volume, RDS poll, signal poll).
  - `src/input.cpp` + `include/input.h` — shared rotary-encoder wrapper + `AdjustMode` enum.
  - `src/test_shield.cpp` — TFT shield bring-up fixture (not a product).
  - `include/ui_layout_tft.h` — TFT UI coordinates, colours, font choices.
  - `include/User_Setup.h` — TFT_eSPI configuration (force-included in both TFT envs).
- **Libraries:** all from PlatformIO registry (PU2CLR SI4735 for both variants, plus Adafruit SSD1306 + GFX for OLED, TFT_eSPI for TFT/shield_test, Ai Esp32 Rotary Encoder for both).
- **Tests:** `test/` — PlatformIO test runner (no tests written yet).
- **Docs:** `docs/` — hardware wiring, firmware architecture, TFT UI spec, shield bring-up, future-improvements roadmap, release runbook.

## Key Implementation Details

- The Si4732 RESET pin is **not connected** to any GPIO. An external RC circuit handles hardware reset. `radio.cpp` bypasses the PU2CLR library's `setup()` → `reset()` flow and calls `radioPowerUp()` directly over I2C.
- Si4732 I2C address is 0x11 (SEN pin wired to GND), set via `setDeviceI2CAddress(0)`.
- Shared I2C bus (SDA=21, SCL=22): on the OLED variant it carries Si4732 + SSD1315; on the TFT variant it carries Si4732 only (the TFT and touch ride on HSPI). The TFT shield uses GPIO 13/14/15/2/33/4/27/17 (see [docs/display_tft.md](docs/display_tft.md)) — all orthogonal to I2C and encoder pins, so both variants reuse the same board and only the physical display differs.
- Encoder uses ISR (`IRAM_ATTR`) on pins 18/19/5 for responsive input; acceleration is enabled.
- OLED variant: single dirty flag (`displayNeedsUpdate`) → full frame buffer + one `display.display()` flush.
- TFT variant: per-zone dirty-flags byte (`DIRTY_HEADER | DIRTY_FREQ | DIRTY_RDS | DIRTY_METER | DIRTY_VOL | DIRTY_FOOTER`) → partial `fillRect` + `drawString` on only the affected zone.
- `radio.cpp` self-rate-limits signal polling to 500 ms and RDS polling to 200 ms. RDS text is mirrored into local buffers and diffed so the UI gets a single clean "changed" signal per update. 10-second sync-loss clears the mirrors.
- TFT touch uses a hard-coded XPT2046 calibration (`{ 477, 3203, 487, 3356, 6 }`) lifted from `src/test_shield.cpp`. Re-run `calibrateTouch()` there if the shield is replaced.

## Code Style

- C++ with Arduino conventions (`setup`/`loop`, `constexpr` for constants)
- Code organized into numbered sections with banner comments
- All pin assignments and tuning parameters are `constexpr` constants at the top of `main.cpp`
- Functions have Doxygen-style doc comments
- All comments and documentation in English

## Versioning & Releases

- **Scheme:** Semantic Versioning (`vMAJOR.MINOR.PATCH`). Git tags on GitHub are the single source of truth for version numbers.
- **Version injection:** `scripts/version.py` is a PlatformIO pre-build script (registered once via `extra_scripts = pre:scripts/version.py` in the `[common]` section, so every env inherits it) that writes `include/version.h` from `git describe --tags --dirty`. The generated file is listed in `.gitignore` — **never commit it, never edit it by hand**.
- **Macros available in code:** `FW_VERSION`, `FW_GIT_COMMIT`, `FW_GIT_DIRTY`, `FW_BUILD_DATE`. Consumed by the `FW_IDENTITY` string in both `src/main.cpp` and `src/main_tft.cpp`, which is kept alive by a no-op inline asm reference in `setup()` (the `used` attribute alone is not enough against `--gc-sections`). Visible via `strings .pio/build/<env>/firmware.elf | grep FW=`. **OLED variant:** not shown on-screen by design — do not add runtime display to `src/main.cpp` unless the user asks. **TFT variant:** `FW_VERSION` is deliberately rendered in the header (top-right) and footer, per the user's v1 spec — this is a conscious per-variant exception, not a policy change for the OLED.
- **Cutting a release** (full checklist in `docs/releasing.md`):
  1. Move `[Unreleased]` items in `CHANGELOG.md` into a new `[X.Y.Z] - YYYY-MM-DD` section; update compare links.
  2. Commit, then `git tag -a vX.Y.Z -m "Release vX.Y.Z"`.
  3. `git push origin master && git push origin vX.Y.Z` — pushing the tag triggers `.github/workflows/release.yml`, which builds both `esp32dev` and `esp32dev_tft` firmware and publishes a GitHub Release with `digi_radio-vX.Y.Z-esp32dev.{bin,elf}` **and** `digi_radio-vX.Y.Z-esp32dev_tft.{bin,elf}` attached.
- **Do not** bump versions by editing files — the only knob is `git tag`.
- **Do not** build `shield_test` env in the release workflow — it is a debug fixture for the Waveshare TFT shield, not a product artifact.
- **Do not** rewrite or force-push a tag that has already been published as a Release — cut a new patch version instead.
