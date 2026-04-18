# ESP32 + Si4732 Digital FM Radio

Fully functional FM radio receiver firmware for ESP32 with a Si4732 radio
chip and rotary-encoder control. Built with PlatformIO and the Arduino
framework. Two peer firmware variants share the same radio and encoder
logic and differ only in the display backend:

- **`esp32dev`** — the original 128×64 SSD1315 OLED build.
- **`esp32dev_tft`** — 240×320 ST7789V TFT shield with RDS, S-meter,
  stereo indicator, and touch. See [docs/display_tft.md](docs/display_tft.md).

## Features

- FM reception across the 87.0–108.0 MHz band with 100 kHz tuning step
- Rotary encoder with acceleration for frequency tuning and volume control
- Dual-mode interface: encoder button toggles between frequency and volume
- No reset GPIO required — external RC circuit handles Si4732 hardware reset
- **OLED variant** — 128×64 display, frequency, volume bar, RSSI bar.
- **TFT variant** — 240×320 colour, large 7-segment frequency, RDS PS + RT,
  S-meter with dBµV scale, stereo / SNR indicator, on-screen version,
  encoder + touch input.

## Hardware

### Components

| Component       | Model / Part        | Interface | Address / Pins     |
|-----------------|---------------------|-----------|--------------------|
| MCU             | ESP32 (DevKit v1)   | —         | —                  |
| Radio receiver  | Si4732              | I2C       | 0x11 (SEN = GND)  |
| OLED display    | SSD1315 (128x64)    | I2C       | 0x3C               |
| Rotary encoder  | Generic with button | GPIO      | A=18, B=19, BTN=5 |

### Wiring

| Signal   | ESP32 GPIO | Connected to     |
|----------|------------|------------------|
| I2C SDA  | 21         | Si4732, OLED     |
| I2C SCL  | 22         | Si4732, OLED     |
| ENC A    | 18         | Encoder A        |
| ENC B    | 19         | Encoder B        |
| ENC BTN  | 5          | Encoder button   |

The Si4732 SEN pin is wired to GND, selecting I2C address 0x11. The RESET
pin is driven by an external RC circuit — see [docs/hardware.md](docs/hardware.md)
for details.

## Build & Flash

Prerequisites: [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
or PlatformIO IDE extension for VS Code.

```bash
pio run -e esp32dev                    # Build OLED firmware
pio run -e esp32dev -t upload          # Build + flash OLED firmware
pio run -e esp32dev_tft                # Build TFT firmware
pio run -e esp32dev_tft -t upload      # Build + flash TFT firmware
```

All library dependencies are fetched automatically by PlatformIO on first build.

## Usage

1. Power on — the display shows a splash screen while the radio initializes.
2. The radio tunes to 102.4 MHz by default.
3. Rotate the encoder to tune through the FM band (87.0–108.0 MHz, wraps around).
4. Press the encoder button to switch to volume mode.
5. Rotate the encoder to adjust volume (0–63).
6. Press the button again to return to frequency mode.

The `>` marker on the OLED indicates which parameter the encoder currently controls.
The RSSI bar in the top-right corner shows the received signal strength.

## Dependencies

| Library                    | Version | Purpose                |
|----------------------------|---------|------------------------|
| PU2CLR SI4735              | ^2.1.8  | Si4732 radio driver    |
| Adafruit SSD1306           | ^2.5.16 | OLED display driver    |
| Adafruit GFX Library       | ^1.12.6 | Graphics primitives    |
| Ai Esp32 Rotary Encoder    | ^1.7    | Rotary encoder driver  |

## Documentation

- [docs/hardware.md](docs/hardware.md) — wiring, components, Si4732 reset circuit
- [docs/firmware.md](docs/firmware.md) — code structure, initialization flow, display layout
- [docs/display_tft.md](docs/display_tft.md) — TFT variant UI spec, touch zones, RDS behaviour
- [docs/display_shield_test.md](docs/display_shield_test.md) — TFT shield bring-up fixture
- [docs/future_improvements.md](docs/future_improvements.md) — v2+ roadmap
- [docs/releasing.md](docs/releasing.md) — how to cut a new release

## Versioning & Releases

This project follows [Semantic Versioning](https://semver.org/). Firmware
versions are tagged `vX.Y.Z`; see [CHANGELOG.md](CHANGELOG.md) for the full
history and [GitHub Releases](https://github.com/aklim/digi_radio_si4732_esp32/releases)
for downloadable binaries. Pushing a `vX.Y.Z` tag automatically triggers a
GitHub Actions build that attaches both `digi_radio-vX.Y.Z-esp32dev.{bin,elf}`
(OLED variant) and `digi_radio-vX.Y.Z-esp32dev_tft.{bin,elf}` (TFT variant)
to the Release.

Each build embeds its version, short commit hash, and build date into the
firmware image via [scripts/version.py](scripts/version.py). Inspect with:

```bash
strings .pio/build/esp32dev/firmware.elf | grep 'FW='
# FW=v1.0.0 commit=abc123d built=2026-04-18
```

Full release procedure: [docs/releasing.md](docs/releasing.md).

## License

[Apache License 2.0](LICENSE)
