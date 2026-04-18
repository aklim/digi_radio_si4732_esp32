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
```

## Architecture

- **Build system:** PlatformIO (`platformio.ini`) with a `[common]` base section and two envs: `esp32dev` (product) and `shield_test` (bring-up fixture). Each env uses an explicit `build_src_filter` to control which `src/*.cpp` files it pulls in.
- **Framework:** Arduino.
- **Sources:**
  - `src/main.cpp` — Arduino entry (`setup` / `loop`), TFT drawing, touch dispatch, menu/input routing.
  - `src/radio.cpp` + `include/radio.h` — Si4732 wrapper: band table, `radioSetBand()`, tune / volume / RDS / signal polling, `radioFormatFrequency()` (mode-aware unit).
  - `src/input.cpp` + `include/input.h` — rotary-encoder wrapper with `ButtonEvent { BTN_NONE, BTN_CLICK, BTN_LONG_PRESS }` (500 ms threshold) and `encoderSetBoundsForMenu()`.
  - `src/menu.cpp` + `include/menu.h` — modal menu state machine (`MENU_TOP` → `MENU_BAND`) + GFX-free-font rendering.
  - `src/persist.cpp` + `include/persist.h` — versioned NVS (Preferences) wrapper with rate-limited writes. Schema guarded by `PERSIST_SCHEMA_VER`.
  - `src/test_shield.cpp` — TFT shield bring-up fixture (not a product).
  - `include/ui_layout.h` — UI coordinates, colours, font choices.
  - `include/User_Setup.h` — TFT_eSPI configuration (force-included via platformio.ini).
- **Libraries:** PU2CLR SI4735, TFT_eSPI (also covers XPT2046 touch), Ai Esp32 Rotary Encoder.
- **Tests:** `test/` — PlatformIO test runner (no tests written yet).
- **Docs:** `docs/` — hardware wiring, firmware architecture, TFT UI spec, menu spec, shield bring-up, future-improvements roadmap, release runbook.

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
- `radio.cpp` self-rate-limits signal polling to 500 ms and RDS polling to 200 ms. RDS + stereo pilot are no-ops on non-FM bands. RDS text is mirrored into local buffers and diffed so the UI gets a single clean "changed" signal per update. 10-second sync-loss clears the mirrors.
- `persist.cpp` coalesces writes with a ≥1 s per-key rate limit so rapid encoder rotation doesn't hammer flash. Schema-version mismatch on boot wipes the namespace and resets to defaults.
- TFT touch uses a hard-coded XPT2046 calibration (`{ 477, 3203, 487, 3356, 6 }`) lifted from `src/test_shield.cpp`. Re-run `calibrateTouch()` there if the shield is replaced.

## Code Style

- C++ with Arduino conventions (`setup`/`loop`, `constexpr` for constants, `static` file-scope helpers).
- Code organized into numbered sections with banner comments.
- All pin assignments and tuning parameters are `constexpr` constants at the top of `main.cpp`.
- Functions have Doxygen-style or concise inline doc comments.
- All comments and documentation in English.

## Versioning & Releases

- **Scheme:** Semantic Versioning (`vMAJOR.MINOR.PATCH`). Git tags on GitHub are the single source of truth for version numbers.
- **Version injection:** `scripts/version.py` is a PlatformIO pre-build script (registered once via `extra_scripts = pre:scripts/version.py` in the `[common]` section, so every env inherits it) that writes `include/version.h` from `git describe --tags --dirty`. The generated file is listed in `.gitignore` — **never commit it, never edit it by hand**.
- **Macros available in code:** `FW_VERSION`, `FW_GIT_COMMIT`, `FW_GIT_DIRTY`, `FW_BUILD_DATE`. Consumed by the `FW_IDENTITY` string in `src/main.cpp`, which is kept alive by a no-op inline asm reference in `setup()` (the `used` attribute alone is not enough against `--gc-sections`). Visible via `strings .pio/build/esp32dev/firmware.elf | grep FW=`. `FW_VERSION` is deliberately rendered in the header (top-right) and footer so the running build is identifiable without a serial monitor.
- **Cutting a release** (full checklist in [docs/releasing.md](docs/releasing.md)):
  1. Move `[Unreleased]` items in `CHANGELOG.md` into a new `[X.Y.Z] - YYYY-MM-DD` section; update compare links.
  2. Commit, then `git tag -a vX.Y.Z -m "Release vX.Y.Z"`.
  3. `git push origin master && git push origin vX.Y.Z` — pushing the tag triggers `.github/workflows/release.yml`, which builds the `esp32dev` firmware and publishes a GitHub Release with `digi_radio-vX.Y.Z-esp32dev.{bin,elf}` attached.
- **Do not** bump versions by editing files — the only knob is `git tag`.
- **Do not** build `shield_test` env in the release workflow — it is a debug fixture for the Waveshare TFT shield, not a product artifact.
- **Do not** rewrite or force-push a tag that has already been published as a Release — cut a new patch version instead.
- **Do not** re-introduce the OLED variant. The 128×64 SSD1315 build has been retired; any future display variant should live on a parallel branch rather than branching back off `src/main.cpp`.
