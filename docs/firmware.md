# Firmware Architecture

## Overview

The entire firmware lives in a single file: `src/main.cpp` (~470 lines).
It is organized into 11 numbered sections with banner comments.

## Code Sections

| #  | Section                | Lines     | Purpose                              |
|----|------------------------|-----------|--------------------------------------|
| 1  | Includes               | 14–19     | Library headers                      |
| 2  | Pin Definitions        | 25–57     | `constexpr` pin and band constants   |
| 3  | Enums & State          | 63–84     | `AdjustMode` enum, global objects    |
| 4  | Encoder ISR            | 95–97     | `IRAM_ATTR` interrupt handler        |
| 5  | Forward Declarations   | 103–117   | Function prototypes                  |
| 6  | `setup()`              | 122–148   | Initialization sequence              |
| 7  | `loop()`               | 154–159   | Main loop (4 function calls)         |
| 8  | Radio Initialization   | 174–208   | Manual Si4732 power-up               |
| 9  | Display Functions      | 218–360   | OLED drawing (5 functions)           |
| 10 | Encoder Functions      | 370–448   | Input handling (4 functions)         |
| 11 | Utility Functions      | 459–471   | RSSI polling                         |

## Initialization Flow (`setup()`)

1. `Serial.begin(115200)` — debug output
2. `Wire.begin(21, 22)` — start shared I2C bus
3. `delay(500)` — wait for the Si4732 RC reset circuit to complete
4. `initDisplay()` — initialize SSD1315, show splash screen
5. `initRadio()` — manual Si4732 power-up sequence:
   - `setDeviceI2CAddress(0)` — address 0x11 (SEN = GND)
   - `setPowerUp(...)` — FM mode, crystal oscillator, analog audio
   - `radioPowerUp()` — send POWER_UP command over I2C
   - `delay(250)` — crystal oscillator stabilization
   - `setVolume(30)`, `getFirmware()`
   - `setFM(8700, 10800, 10240, 10)` — band limits and default frequency
6. `initEncoder()` — attach ISR, set acceleration, set frequency bounds
7. Read initial frequency and volume from the radio chip

## Main Loop (`loop()`)

Each iteration calls four functions in order:

| Function          | Purpose                                        |
|-------------------|------------------------------------------------|
| `handleEncoder()` | Check for rotation, update frequency or volume |
| `handleButton()`  | Check for press, toggle adjustment mode        |
| `updateRSSI()`    | Poll signal quality every 500 ms               |
| `updateDisplay()` | Redraw OLED if `displayNeedsUpdate` is set     |

## Display Layout

The SSD1315 panel has a yellow zone (top 16 px) and a blue zone (bottom 48 px).

```
+------------------------------+
| FM          [===RSSI===]     |  y = 0..15   (yellow)
|------------------------------|
| > 102.4 MHz                  |  y = 22..38  (blue)
|                              |
| > Vol [========--] 30        |  y = 52..60  (blue)
+------------------------------+
```

- **FM** — radio mode label (top-left)
- **RSSI bar** — signal strength indicator (top-right, 0–60 dBuV range)
- **Frequency** — large text, e.g. "102.4 MHz"
- **Volume bar** — filled bar + numeric value (0–63)
- **`>`** marker — appears next to the parameter currently controlled by the encoder

## Control Modes

The encoder button toggles between two modes:

| Mode      | Encoder action       | Range            | Wrapping | Acceleration |
|-----------|----------------------|------------------|----------|--------------|
| Frequency | Tune FM band         | 87.0–108.0 MHz   | Yes      | 100          |
| Volume    | Adjust audio volume  | 0–63             | No       | 50           |

In frequency mode the encoder value wraps around at the band edges.
In volume mode the value clamps at 0 and 63.
