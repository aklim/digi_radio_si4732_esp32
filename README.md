# ESP32 + Si4732 Digital FM Radio

Fully functional FM radio receiver firmware for ESP32 with a Si4732 radio
chip, SSD1315 OLED display, and rotary encoder control. Built with
PlatformIO and the Arduino framework.

## Features

- FM reception across the 87.0–108.0 MHz band with 100 kHz tuning step
- 128x64 OLED display showing frequency, volume bar, and RSSI signal strength
- Rotary encoder with acceleration for frequency tuning and volume control
- Dual-mode interface: encoder button toggles between frequency and volume
- No reset GPIO required — external RC circuit handles Si4732 hardware reset

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
pio run                    # Build firmware
pio run --target upload    # Build and flash to ESP32
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

## License

[Apache License 2.0](LICENSE)
