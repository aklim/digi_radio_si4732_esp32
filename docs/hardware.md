# Hardware

## Components

| Component       | Model / Part        | Interface | Address / Pins      |
|-----------------|---------------------|-----------|---------------------|
| MCU             | ESP32 (DevKit v1)   | —         | —                   |
| Radio receiver  | Si4732              | I2C       | 0x11 (SEN = GND)   |
| OLED display    | SSD1315 (128x64)    | I2C       | 0x3C                |
| Rotary encoder  | Generic with button | GPIO      | A=18, B=19, BTN=5  |

## Wiring

The Si4732 and OLED share a single I2C bus.

| Signal   | ESP32 GPIO | Connected to        |
|----------|------------|---------------------|
| I2C SDA  | 21         | Si4732, OLED        |
| I2C SCL  | 22         | Si4732, OLED        |
| ENC A    | 18         | Encoder channel A   |
| ENC B    | 19         | Encoder channel B   |
| ENC BTN  | 5          | Encoder push button |

```
ESP32 GPIO21 (SDA) ──── Si4732 SDA ──── OLED SDA
ESP32 GPIO22 (SCL) ──── Si4732 SCL ──── OLED SCL
ESP32 GPIO18 ─────────── Encoder A
ESP32 GPIO19 ─────────── Encoder B
ESP32 GPIO5  ─────────── Encoder Button

Si4732 SEN ──── GND   (selects I2C address 0x11)
```

The rotary encoder is powered externally (VCC pin is not connected to a GPIO).

## Si4732 Reset Circuit

The Si4732 RESET pin is **not connected** to any ESP32 GPIO. Instead, an
external RC circuit on the RESET pin provides automatic hardware reset at
power-on.

**Why this design:** It frees up a GPIO pin and simplifies the PCB layout.
The trade-off is that the MCU cannot force a hard reset of the radio chip
at runtime — only a full power cycle will reset it.

**Firmware compensation:**

1. A 500 ms delay after boot lets the RC circuit finish the reset.
2. The PU2CLR SI4735 library's `setup()` method is **not** called because
   it internally calls `reset()`, which toggles a GPIO that does not exist.
3. Instead, the firmware calls the library methods directly:
   `setDeviceI2CAddress(0)` → `setPowerUp(...)` → `radioPowerUp()` →
   `setVolume()` → `getFirmware()` → `setFM(...)`.
4. After this manual init, all other library methods work normally — they
   communicate over I2C and never call `reset()` again.

## OLED Display

- **Controller:** SSD1315 (drop-in replacement for SSD1306)
- **Resolution:** 128 x 64 pixels
- **Dual-color panel:** top 16 rows are yellow, bottom 48 rows are blue.
  This is a hardware characteristic of the panel, not software-controlled.
- The Adafruit SSD1306 library works unchanged with SSD1315 panels.
