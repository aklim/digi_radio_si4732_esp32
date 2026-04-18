# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fully functional FM radio receiver firmware using the Silicon Labs Si4732 chip with an ESP32 microcontroller. Single-file firmware (`src/main.cpp`, ~470 lines) that handles FM tuning, volume control, OLED display, and rotary encoder input. Built with PlatformIO and the Arduino framework.

## Build Commands

```bash
pio run                        # Build firmware
pio run --target upload        # Build and flash to ESP32
pio run --target size          # Show program size
```

## Architecture

- **Build system:** PlatformIO (`platformio.ini`) targeting `esp32dev` board
- **Framework:** Arduino (entry point is `setup()` / `loop()` in `src/main.cpp`)
- **Source:** `src/main.cpp` — single-file firmware (~470 lines), organized into 11 numbered sections
- **Libraries:** all from PlatformIO registry (PU2CLR SI4735, Adafruit SSD1306, Adafruit GFX, Ai Esp32 Rotary Encoder); no custom libraries in `lib/`
- **Tests:** `test/` — PlatformIO test runner (no tests written yet)
- **Headers:** `include/` — shared header files (unused)
- **Docs:** `docs/` — hardware wiring and firmware architecture reference

## Key Implementation Details

- The Si4732 RESET pin is **not connected** to any GPIO. An external RC circuit handles hardware reset. The firmware bypasses the PU2CLR library's `setup()` → `reset()` flow and calls `radioPowerUp()` directly over I2C.
- Si4732 I2C address is 0x11 (SEN pin wired to GND), set via `setDeviceI2CAddress(0)`.
- I2C bus is shared between Si4732 and SSD1315 OLED (SDA=21, SCL=22).
- Encoder uses ISR (`IRAM_ATTR`) on pins 18/19 for responsive input; acceleration is enabled.
- Display updates use a dirty flag (`displayNeedsUpdate`) to avoid unnecessary redraws.
- RSSI is polled every 500 ms via `getCurrentReceivedSignalQuality()`.

## Code Style

- C++ with Arduino conventions (`setup`/`loop`, `constexpr` for constants)
- Code organized into numbered sections with banner comments
- All pin assignments and tuning parameters are `constexpr` constants at the top of `main.cpp`
- Functions have Doxygen-style doc comments
- All comments and documentation in English

## Versioning & Releases

- **Scheme:** Semantic Versioning (`vMAJOR.MINOR.PATCH`). Git tags on GitHub are the single source of truth for version numbers.
- **Version injection:** `scripts/version.py` is a PlatformIO pre-build script (registered via `extra_scripts = pre:scripts/version.py` in both `env:esp32dev` and `env:shield_test`) that writes `include/version.h` from `git describe --tags --dirty`. The generated file is listed in `.gitignore` — **never commit it, never edit it by hand**.
- **Macros available in code:** `FW_VERSION`, `FW_GIT_COMMIT`, `FW_GIT_DIRTY`, `FW_BUILD_DATE`. Consumed by the `FW_IDENTITY` string in `src/main.cpp`, which is kept alive by a no-op inline asm reference in `setup()` (the `used` attribute alone is not enough against `--gc-sections`). Visible via `strings .pio/build/esp32dev/firmware.elf | grep FW=`. **Not shown on OLED or printed to Serial by design** — do not add runtime display unless the user asks.
- **Cutting a release** (full checklist in `docs/releasing.md`):
  1. Move `[Unreleased]` items in `CHANGELOG.md` into a new `[X.Y.Z] - YYYY-MM-DD` section; update compare links.
  2. Commit, then `git tag -a vX.Y.Z -m "Release vX.Y.Z"`.
  3. `git push origin master && git push origin vX.Y.Z` — pushing the tag triggers `.github/workflows/release.yml`, which builds the `esp32dev` firmware and publishes a GitHub Release with `digi_radio-vX.Y.Z-esp32dev.{bin,elf}` attached.
- **Do not** bump versions by editing files — the only knob is `git tag`.
- **Do not** build `shield_test` env in the release workflow — it is a debug fixture for the Waveshare TFT shield, not a product artifact.
- **Do not** rewrite or force-push a tag that has already been published as a Release — cut a new patch version instead.
