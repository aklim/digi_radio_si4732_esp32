# ESP32 + Si4732 Digital Radio

Fully functional FM / MW / SW radio receiver firmware for ESP32 with a
Si4732 SSB-capable radio chip, a Waveshare 2.8" ST7789V TFT touch shield,
and a rotary encoder. Built with PlatformIO and the Arduino framework.
The firmware is targeting feature / UI parity with
[ATS-Mini](https://github.com/esp32-si4732/ats-mini) — see
[docs/future_improvements.md](docs/future_improvements.md) for the roadmap.

## Features

- **Multi-band reception** — FM Broadcast (87.0–108.0 MHz), MW
  (520–1710 kHz), SW 41 m (7100–7300 kHz), SW 31 m (9400–9900 kHz).
  LW, additional SW windows, and SSB/BFO are on the roadmap.
- **Dual-core task split** — Si4732 I²C polling (signal 500 ms, RDS 200 ms)
  runs on a dedicated FreeRTOS task pinned to Core 0; the Arduino loop
  on Core 1 owns UI, input, and persistence, so I²C work never stalls
  the display.
- **ATS-Mini-parity landscape UI** (320×240, rotation 3) — segmented
  S-meter with stereo-pilot split, sidebar info box
  (Step / BW / AGC / Vol / PI / Time), big 7-segment frequency readout,
  RDS PS + RadioText, band scale with tick marks, header status icons
  (RDS / BLE / WiFi / battery), and nine bundled colour themes
  (Default / Bluesky / eInk / Pager / Orange / Night / Phosphor /
  Space / Magenta).
- **Encoder long-press menu** — full modal menu with submenus for
  Band, BW, AGC, Theme, Scan (bandscope sweep), Memory (16-slot
  presets), and Settings (RDS / BT / WiFi toggles + Backlight
  brightness). Rotation navigates; click confirms; long-press exits.
- **Bandwidth + AGC** — per-mode IF filter (FM: 5 presets; AM/SW:
  7 presets, 1.0 kHz … 6.0 kHz) and independent AGC / manual-attenuator
  shadow for FM vs AM, both persisted across band switches.
- **Bandscope sweep** — 200-point RSSI + SNR plot around the current
  tune, ATS-Mini-compatible rendering; rotate to re-centre on a new
  frequency, click to commit, long-press to cancel.
- **RDS decoding** — Programme Service name, RadioText, stereo pilot
  indicator (FM only). Triple-gate sanitiser (sync + printable-text +
  whitespace scrub) means the UI only ever sees real printable content.
  RDS can be toggled off from **Settings → RDS** to cut I²C traffic.
- **5-button transport-style touch row** — fingertip-sized icon buttons
  at the bottom of the screen: `⏪ Seek Down`, `◀ Prev Preset`,
  `🔊 Mute`, `▶ Next Preset`, `⏩ Seek Up`. Icons drawn procedurally
  so they adapt to every theme without bitmap-font baggage.
- **Auto-seek** — outer ⏪ / ⏩ buttons step through the current band
  until an RSSI + SNR threshold is met (FM: ≥15 dBµV / 8 dB; AM/SW:
  ≥25 / 8), with a 3-step peak-climb so adjacent-channel bleed doesn't
  land us 100 kHz off the real carrier. Wraps at band edges; any
  encoder or touch input aborts.
- **User-latched mute** — centre 🔊 button toggles audio without
  touching volume. Latch survives band switches and bandscope sweeps
  (scan-exit restores the latch rather than unconditionally
  un-muting). Not persisted on purpose — mute-on-reboot would be
  user-hostile.
- **Memory presets** — 16 slots saved to NVS, managed from
  `Menu → Memory` (Load / Save / Delete per slot). Inner ◀ / ▶
  transport buttons jump through the presets saved for the current
  band, ordered by frequency and wrapping at the ends.
- **Power reduction** — CPU clocked at 80 MHz (ample for this workload
  and the documented minimum for the ESP32 WiFi/BT stacks), a single
  `vTaskDelay(1)` per main-loop iteration lets Core 1's idle task enter
  low-power sleep, backlight default at 55 % (user-adjustable via
  `Settings → Brightness`), BT + WiFi default OFF and physically
  teardown via `connectivity.cpp` when disabled.
- **XPT2046 resistive touch** — landscape touch coordinates are
  remapped at runtime (the calibration was captured in rotation 0 on
  the shield bring-up fixture) so every hit-test lands on its true
  on-screen target.
- **NVS persistence (schema v6)** — current band, per-band tuned
  frequency, volume, theme, BW/AGC per mode, RDS/BT/WiFi enables,
  backlight percent, and all 16 preset slots survive reboots. Writes
  are rate-limited (≥1 s per key) so rapid encoder rotation doesn't
  hammer flash; schema-version mismatch cleanly wipes and re-seeds
  defaults.
- **No reset GPIO required** — external RC circuit handles Si4732
  hardware reset so every MCU pin stays available for the radio and
  display wiring.

## Hardware

### Components

| Component       | Model / Part              | Interface | Address / Pins      |
|-----------------|---------------------------|-----------|---------------------|
| MCU             | ESP32 (DevKit v1)         | —         | —                   |
| Radio receiver  | Si4732                    | I²C       | 0x11 (SEN = GND)    |
| Display shield  | Waveshare 2.8" ST7789V    | HSPI      | 320×240 landscape (rotation 3) |
| Touch           | XPT2046 (on the shield)   | HSPI      | CS = GPIO 17        |
| Rotary encoder  | Generic with button       | GPIO      | A=18, B=19, BTN=5   |

### Wiring

| Signal       | ESP32 GPIO | Connected to              |
|--------------|------------|---------------------------|
| I²C SDA      | 21         | Si4732                    |
| I²C SCL      | 22         | Si4732                    |
| TFT MOSI     | 13         | Shield MOSI               |
| TFT SCLK     | 14         | Shield SCLK               |
| TFT MISO     | 27         | Shield MISO (shared XPT)  |
| TFT CS       | 15         | Shield display CS         |
| TFT DC       | 2          | Shield DC                 |
| TFT RST      | 33         | Shield RST                |
| TFT BL (PWM) | 4          | Shield backlight (LEDC)   |
| Touch CS     | 17         | Shield XPT2046 CS         |
| ENC A        | 18         | Encoder A                 |
| ENC B        | 19         | Encoder B                 |
| ENC BTN      | 5          | Encoder button            |

The Si4732 SEN pin is wired to GND, selecting I²C address 0x11. The
RESET pin is driven by an external RC circuit — see
[docs/hardware.md](docs/hardware.md) for the schematic.

## Build & Flash

Prerequisites: [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
or the PlatformIO IDE extension for VS Code.

```bash
pio run -e esp32dev                  # Build firmware
pio run -e esp32dev -t upload        # Build + flash
pio run -e shield_test -t upload     # Shield bring-up fixture (not a product)
```

All library dependencies are fetched automatically by PlatformIO on
first build.

## Tests

Pure host-side logic runs under PlatformIO's Unity test runner on the
developer's machine — no ESP32 hardware required. Five suites cover
the non-hardware-dependent units:

| Suite                | Covers                                                      |
|----------------------|-------------------------------------------------------------|
| `test_native_format` | `radioFormatFrequencyPure()` mode-aware freq formatting     |
| `test_native_rds`    | `rdsSanitizeRt()` triple-gate RadioText sanitiser           |
| `test_native_bands`  | `g_bands[]` table invariants (range, step, default freq)    |
| `test_native_preset` | `presetPack()` / `presetUnpack()` NVS bit-layout codec      |
| `test_native_seek`   | `seekNextFreq()` auto-seek wrap arithmetic (up/down/edge)   |

```bash
pio test -e native                   # Run all native suites (~7 s)
```

[.github/workflows/ci.yml](.github/workflows/ci.yml) runs the same
suites on every push and pull request to `master`, so regressions in
the tested units block merges. See
[docs/firmware.md](docs/firmware.md#tests) for how to add a new suite.

## Usage

1. Power on — splash screen, then the main UI within ~1 s.
2. Rotate the encoder to tune within the current band (clamps at
   band edges) or adjust volume (0..63) depending on the active
   mode. The sidebar's `Vol:` row is bolded when the encoder drives
   volume, so the current mode is visible at a glance.
3. Short-click the encoder to toggle between frequency and volume
   adjustment.
4. **Long-press** (≥500 ms) the encoder to open the modal menu.
   Rotate to move the highlight, click to confirm, long-press to
   back out. Submenus:
   - `Band` — switch between FM Broadcast / MW / SW 41m / SW 31m
     (restores the band's last tuned freq).
   - `BW` — IF filter per mode (FM: Auto/110k/84k/60k/40k; AM/SW:
     1.0k..6.0k in 7 steps).
   - `AGC` — AGC on, or AGC off with a manual attenuator step per
     mode (FM: 0..27; AM: 0..37).
   - `Theme` — pick one of nine bundled colour palettes.
   - `Scan` — bandscope sweep around the current tune. Rotate to
     re-centre after the sweep, click to commit the new tune,
     long-press to cancel.
   - `Memory` — 16 preset slots. Click a slot to Load / Save
     current / Delete. Slots store `band + freq` only.
   - `Settings` — toggles for RDS / Bluetooth / WiFi, plus a
     5-step Backlight brightness picker (20 / 40 / 60 / 80 /
     100 %).
5. Tap the frequency or volume region of the display (upper-mid
   strip) to switch the encoder's adjustment mode by touch — same
   effect as the short-click.
6. Use the transport-style row along the bottom of the screen for
   one-touch actions that don't need the menu:
   - `⏪` / `⏩` — auto-seek down / up through the current band
     until a listenable signal is found (wraps at band edges; any
     encoder or touch input aborts).
   - `◀` / `▶` — jump to the previous / next saved preset **for
     the current band** (in frequency order, wraps). Silent no-op
     when no presets are saved for the current band.
   - `🔊` — toggle mute without changing volume. Muted state shows
     an inverted button fill plus a slash across the speaker icon.
7. Band, per-band tune, volume, BW, AGC, theme, RDS/BT/WiFi
   enables, backlight percent, and all 16 preset slots auto-save
   to NVS and are restored on the next boot.

See [docs/menu.md](docs/menu.md) for the full menu UX spec and
band-table details.

## Dependencies

| Library                    | Version | Purpose                 |
|----------------------------|---------|-------------------------|
| PU2CLR SI4735              | ^2.1.8  | Si4732 radio driver     |
| TFT_eSPI                   | ^2.5.43 | TFT + XPT2046 touch     |
| Ai Esp32 Rotary Encoder    | ^1.7    | Rotary encoder driver   |

## Documentation

- [docs/hardware.md](docs/hardware.md) — wiring, components, Si4732 reset circuit
- [docs/firmware.md](docs/firmware.md) — code structure, initialization flow
- [docs/display_tft.md](docs/display_tft.md) — TFT UI spec, touch zones, RDS behaviour
- [docs/menu.md](docs/menu.md) — long-press menu + band table
- [docs/display_shield_test.md](docs/display_shield_test.md) — TFT shield bring-up fixture
- [docs/future_improvements.md](docs/future_improvements.md) — ATS-Mini parity roadmap
- [docs/releasing.md](docs/releasing.md) — how to cut a new release
- [docs/firmware.md § Tests](docs/firmware.md#tests) — native Unity suites covered by CI

## Versioning & Releases

This project follows [Semantic Versioning](https://semver.org/). Firmware
versions are tagged `vX.Y.Z`; see [CHANGELOG.md](CHANGELOG.md) for the
full history and
[GitHub Releases](https://github.com/aklim/digi_radio_si4732_esp32/releases)
for downloadable binaries. Pushing a `vX.Y.Z` tag automatically triggers a
GitHub Actions build that attaches `digi_radio-vX.Y.Z-esp32dev.{bin,elf}`
to the Release.

Each build embeds its version, short commit hash, and build date into
the firmware image via [scripts/version.py](scripts/version.py). Inspect
with:

```bash
strings .pio/build/esp32dev/firmware.elf | grep 'FW='
# FW=v1.1.0-4-gABCDEF1 commit=abcdef1 built=2026-04-18
```

Full release procedure: [docs/releasing.md](docs/releasing.md).

## License

[Apache License 2.0](LICENSE)
