# Future Improvements

Living roadmap of ideas beyond the scope that shipped with the initial TFT
firmware. Each bullet is intentionally short; elaboration happens when an
item gets picked up. Ideas drawn from the
[ATS-Mini](https://atsmini.github.io/) and H.-J. Berndt's
[pocketSI4735DualCoreDecoder](http://www.hjberndt.de/dvb/pocketSI4735DualCoreDecoder.html)
references plus gaps surfaced while building v1.

## UI / rendering

- Mini-waterfall / bandscope from RSSI history (circular buffer 240×N,
  colour-mapped per dBµV) — Berndt-style without I/Q.
- Animated analog S-meter needle as an alternative to the bar.
- RadioText marquee scroll when the text exceeds the visible width.
- FreeFonts pack for nicer typography (Orbitron / DSeg7 for frequency,
  FreeSansBold for labels).
- Dark / light themes with per-theme colour tables.
- Battery icon with SOC percentage (blocked on hardware — see below).
- On-screen "About" / firmware info dialog (long-press encoder or top-bar tap).
- Frequency-step change affordance on screen (10 kHz / 50 kHz / 100 kHz).
- Flicker-free redraw via `TFT_eSprite` per zone + DMA push (pattern
  already proven in `src/test_shield.cpp` phase 5/6).

## Radio features

- AM / SW / LW bands — `radio.cpp` API extension + UI band label.
- SSB (LSB / USB) with BFO tuning — requires the library's SSB patch-load
  sequence, bigger lift.
- Squelch by RSSI threshold.
- RDS PTY (Programme Type) decode — the companion field to CT that
  names the programme genre. CT itself landed in v2.8.0.
- Authoritative time source as a fallback when RDS CT is wrong or
  absent. Many commercial / regional broadcasters ship bogus CT (UTC
  mislabelled as local, stale DST, or just unsynced station clocks),
  so CT alone can't be trusted as a clock for the listener. Two paths:
  WiFi + NTP (requires bringing up the STA connect flow that
  `connectivity.cpp` currently scaffolds but does not wire) or a
  small battery-backed RTC module on an unused GPIO. In either model
  RDS CT becomes an optional overlay (per-station sanity check), not
  the primary source.
- Manual TZ override in Settings — a simpler, offline-only
  compromise: user picks `Auto / UTC / UTC+1 / UTC+2 / UTC+3 / UTC+4`
  and we shift the decoded CT by that amount. Does not help the
  per-station-disagreement case (different stations still disagree
  with each other), but lets listeners who have a "home" station
  with otherwise-correct UTC timestamps get real local time.

## Input

- Long-press encoder → menu system (ATS-Mini pattern).
- Double-click encoder → secondary action per current mode.
- Touch gestures: swipe left / right on freq zone = fine tune.
- Capacitive-touch upgrade path (XPT2046 is resistive — documented for
  posterity).

## Performance / architecture

- Move radio poll + UI render onto a dedicated `xTaskCreatePinnedToCore`
  task on Core 1 (Berndt pattern). Core 0 handles Arduino loop + Wi-Fi if
  ever added.
- DMA-backed full-screen sprite for the frequency zone to eliminate any
  residual flicker.
- Measure and log UI frame time to Serial for regression tracking.
- Benchmark current vs sprite pipeline under realistic RDS + RSSI load.

## Hardware

- LiPo + voltage divider → ADC → replace `"USB"` footer with a real
  percentage; add a charge-state glyph if a charger IC is added.
- External-antenna-switch UI affordance if a relay is added.
- Headphone-jack detect (GPIO) → display icon + volume policy.
- PWM-controlled backlight brightness (`TFT_BL` on GPIO 4 already uses
  LEDC — expose as a setting).
- Migrate off the ESP32 DevKit to an ESP32-S3 (PSRAM + more RAM) — would
  unlock LVGL, a higher-FPS waterfall, and parity with ATS-Mini hardware.
  Document as a separate hardware revision.

## Release / CI

- Native Unity unit tests for the remaining time- / hardware-dependent
  units — extend the initial suite in `test/test_native_*/` (covers
  `radioFormatFrequencyPure`, `rdsSanitizeRt`, and the `g_bands[]`
  invariants; see [firmware.md § Tests](firmware.md#tests)) to:
  - Encoder click vs long-press state machine (`encoderPollButton()`
    in `input.cpp`) — needs a `millis()` injection seam first.
  - NVS rate-limiting in `persist.cpp` — needs `millis()` + a mock
    `Preferences`.
  - Menu viewport / cursor logic in `menu.cpp` — needs a reset entry
    point for its namespaced statics.
- GitHub Actions job to diff `.text` / `.data` size vs the previous tag and
  warn on regressions.
- Upload-to-Release step that posts `User_Setup.h` and `platformio.ini`
  alongside the binaries for reproducibility.
- Nightly build of `master` publishing a "dev" artifact (rolling tag).
- Static analysis (`cppcheck` / `clang-tidy`) on PRs.
- Automated hardware-in-the-loop smoke test via a USB relay + Serial capture
  when practical.

## Documentation

- Photos of the assembled TFT variant in [hardware.md](hardware.md).
- Short video / GIF of the UI in action linked from the top-level README.
- Troubleshooting section for common shield issues (inverted colours →
  `TFT_INVERSION_ON`, black screen → backlight GPIO, touch drifting →
  recalibrate).
