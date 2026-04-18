# Hardware

## Components

| Component       | Model / Part              | Interface | Address / Pins      |
|-----------------|---------------------------|-----------|---------------------|
| MCU             | ESP32 (DevKit v1)         | —         | —                   |
| Radio receiver  | Si4732                    | I²C       | 0x11 (SEN = GND)    |
| Display shield  | Waveshare 2.8" ST7789V    | HSPI      | 240×320, rotation 0 |
| Touch           | XPT2046 (on the shield)   | HSPI      | CS = GPIO 17        |
| Rotary encoder  | Generic with button       | GPIO      | A=18, B=19, BTN=5   |

## Wiring

The Si4732 owns the I²C bus on its own. The TFT shield and its XPT2046
touch controller ride on HSPI; the two buses are independent, so neither
starves the other during UI redraws or RDS polling.

### Si4732 + encoder (I²C / GPIO)

| Signal   | ESP32 GPIO | Connected to        |
|----------|------------|---------------------|
| I²C SDA  | 21         | Si4732 SDA          |
| I²C SCL  | 22         | Si4732 SCL          |
| ENC A    | 18         | Encoder channel A   |
| ENC B    | 19         | Encoder channel B   |
| ENC BTN  | 5          | Encoder push button |

```
ESP32 GPIO21 (SDA) ──── Si4732 SDA
ESP32 GPIO22 (SCL) ──── Si4732 SCL
ESP32 GPIO18       ──── Encoder A
ESP32 GPIO19       ──── Encoder B
ESP32 GPIO5        ──── Encoder Button

Si4732 SEN ──── GND   (selects I²C address 0x11)
```

The rotary encoder is powered externally (its VCC pin is not connected
to a GPIO).

### TFT Touch Shield (HSPI)

The Waveshare 2.8" TFT Touch Shield Rev 2.1 stacks on top of the ESP32
DevKit. SB1 / SB2 / SB3 solder jumpers must be bridged; power is shared
via the ESP32's 5V / VIN rail and a common GND.

| Signal      | ESP32 GPIO | Note                                |
|-------------|------------|-------------------------------------|
| TFT MOSI    | 13         | HSPI IOMUX (direct-connect)         |
| TFT SCLK    | 14         | HSPI IOMUX                          |
| TFT MISO    | 27         | Shared with XPT2046 read path       |
| TFT CS      | 15         | HSPI                                |
| TFT DC      |  2         |                                     |
| TFT RST     | 33         |                                     |
| TFT BL      |  4         | LEDC PWM backlight                  |
| Touch CS    | 17         | XPT2046                             |

Driver configuration (ST7789V, BGR, inversion on, HSPI, 27 MHz) is in
[include/User_Setup.h](../include/User_Setup.h). UI layout and touch
zones are in [display_tft.md](display_tft.md).

## Si4732 Reset Circuit

The Si4732 RESET pin is **not connected** to any ESP32 GPIO. Instead, an
external RC circuit on the RESET pin provides automatic hardware reset
at power-on.

**Why this design:** It frees up a GPIO pin and simplifies the PCB
layout. The trade-off is that the MCU cannot force a hard reset of the
radio chip at runtime — only a full power cycle will reset it.

**Firmware compensation:**

1. A 500 ms delay after boot lets the RC circuit finish the reset.
2. The PU2CLR SI4735 library's `setup()` method is **not** called
   because it internally calls `reset()`, which toggles a GPIO that
   does not exist.
3. Instead, the firmware calls the library methods directly:
   `setDeviceI2CAddress(0)` → `setPowerUp(...)` → `radioPowerUp()` →
   `setVolume()` → `getFirmware()` → `setFM(...)` / `setAM(...)` per
   band.
4. After this manual init, all other library methods work normally —
   they communicate over I²C and never call `reset()` again.
