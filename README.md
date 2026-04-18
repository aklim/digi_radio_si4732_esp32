# ESP32 + Si4732 Digital Radio

Fully functional FM / MW / SW radio receiver firmware for ESP32 with a
Si4732 SSB-capable radio chip, a Waveshare 2.8" ST7789V TFT touch shield,
and a rotary encoder. Built with PlatformIO and the Arduino framework.
The firmware is targeting feature / UI parity with
[ATS-Mini](https://github.com/esp32-si4732/ats-mini) — see
[docs/future_improvements.md](docs/future_improvements.md) for the roadmap.

## Features

- FM Broadcast (87.0–108.0 MHz), MW (520–1710 kHz), SW 41 m
  (7100–7300 kHz), SW 31 m (9400–9900 kHz). LW + additional SW + SSB/BFO
  are on the roadmap.
- Encoder **long-press menu** for band switching; more menu items land
  as features ship.
- **Analog S-meter** — sprite-backed semi-arc gauge with a green → yellow →
  red needle that animates smoothly between RSSI samples.
- **RDS** — Programme Service name, RadioText, stereo pilot indicator
  (FM only).
- **NVS persistence** — last band, per-band last-tuned frequency, and
  volume survive reboots. Schema-versioned so future changes can migrate
  cleanly.
- **Touch input** — XPT2046 resistive panel taps select frequency /
  volume mode as an alternative to the encoder button.
- No reset GPIO required — external RC circuit handles Si4732 hardware
  reset.

## Hardware

### Components

| Component       | Model / Part              | Interface | Address / Pins      |
|-----------------|---------------------------|-----------|---------------------|
| MCU             | ESP32 (DevKit v1)         | —         | —                   |
| Radio receiver  | Si4732                    | I²C       | 0x11 (SEN = GND)    |
| Display shield  | Waveshare 2.8" ST7789V    | HSPI      | 240×320, rotation 0 |
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

## Usage

1. Power on — splash screen, then the main UI within ~1 s.
2. Rotate the encoder to tune within the current band (wraps at band
   edges).
3. Short-click the encoder to toggle between frequency and volume
   adjustment; the focus border (yellow vs grey) shows which zone the
   encoder drives.
4. **Long-press** (≥500 ms) the encoder to open the menu. Rotate to
   move the highlight, click to confirm. Pick a band from `Band →
   [FM Broadcast / MW / SW 41m / SW 31m]` or choose `Close` to exit.
5. Tap the frequency or volume zone on the screen to switch mode via
   touch (same effect as the encoder button).
6. Current band + tune + volume auto-save to NVS and are restored on
   next boot.

See [docs/menu.md](docs/menu.md) for the full UX and band table.

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
