# Display Shield Test

Standalone bring-up and benchmark program for the **Waveshare 2.8" TFT
Touch Shield Rev 2.1** (SKU 10684) on an ESP32 DevKit V1. Lives in
`src/test_shield.cpp` under its own PlatformIO environment so it does
not interfere with the main radio firmware.

## Purpose

This env exists to validate the Waveshare 2.8" TFT shield hardware
independently of the radio firmware. Retained from the original
bring-up — the shipping firmware now drives the shield directly. Before
rewriting the radio UI for the larger colour display we needed to
confirm:

1. The panel powers up and responds over SPI.
2. Colors, fonts, primitives, and PWM backlight work.
3. The resistive touch layer is usable.
4. Rendering performance is adequate, and whether DMA is worth the extra
   complexity in the radio firmware.

## Hardware

| Component       | Part              | Interface   |
|-----------------|-------------------|-------------|
| Display panel   | ST7789V, 240x320  | SPI mode 3  |
| Touch panel     | XPT2046 resistive | SPI (shared)|
| SD card slot    | standard micro-SD | not tested  |
| Backlight       | LED + transistor  | PWM input   |

Logic level is 3.3 V (ESP32-native). The shield's on-board LDO needs
5 V on the `5V` pin to power the panel and touch IC.

### Wiring

| Shield label | Signal                    | ESP32 GPIO | Notes                 |
|--------------|---------------------------|------------|-----------------------|
| `SCLK`       | SPI clock                 | 14         | HSPI IOMUX native     |
| `MOSI`       | SPI data to shield        | 13         | HSPI IOMUX native     |
| `MISO`       | SPI data from shield      | 27         | remapped via matrix   |
| `LCD_CS`     | LCD chip select           | 15         | HSPI native CS        |
| `LCD_DC`     | LCD command/data          | 2          |                       |
| `LCD_RST`    | LCD reset                 | 33         |                       |
| `LCD_BL`     | LCD backlight             | 4          | LEDC PWM, 5 kHz, 8-bit|
| `TP_CS`      | Touch chip select         | 17         |                       |
| `TP_IRQ`     | Touch interrupt           | unused     | TFT_eSPI polls by CS  |
| `SD_CS`      | SD card CS                | unused     | SD not tested         |
| `5V`         | +5 V supply               | 5V / VIN   |                       |
| `GND`        | Ground                    | GND        | must be shared        |

The shield is designed as an Arduino UNO shield; with a bare ESP32 each
labeled pin is wired individually to the GPIO above.

### Preflight checklist

Before powering the shield:

1. **SB1, SB2, SB3 solder jumpers** on the shield (next to the SD slot)
   must be **bridged**. When no Arduino ICSP header is present — i.e.
   our wiring — the three SPI signals only reach the LCD controller
   through these jumpers. If they are open, the panel is silent no
   matter what firmware does.
2. Shared GND between ESP32 and shield, confirmed with a multimeter.
3. Shield `5V` supplied from ESP32 `5V` or `VIN`.
4. `MISO` and `MOSI` are not swapped (the single most common mistake).

## Software

- **Library:** `bodmer/TFT_eSPI@^2.5.43` — chosen over Adafruit_ST7789
  because it has a hardware-optimized fill path, DMA support, smooth
  fonts, and built-in XPT2046 driver.
- **Configuration:** `include/User_Setup.h` — force-included by
  PlatformIO for this env (not shared with `env:esp32dev`).
- **Environment:** `[env:shield_test]` in `platformio.ini` — builds only
  `src/test_shield.cpp` via `build_src_filter`.

### Key `User_Setup.h` choices

| Setting              | Value        | Reason                                    |
|----------------------|--------------|-------------------------------------------|
| `ST7789_DRIVER`      | defined      | Rev 2.1 uses ST7789V                      |
| `USE_HSPI_PORT`      | defined      | SCK/MOSI/CS on HSPI IOMUX = better timing |
| `TFT_RGB_ORDER`      | `TFT_BGR`    | Rev 2.1 wants BGR; flip if colors swap    |
| `TFT_INVERSION_ON`   | defined      | without it the image looks like a negative|
| `TFT_SPI_MODE`       | `SPI_MODE3`  | ST7789V on this shield latches on falling |
| `SPI_FREQUENCY`      | 27 MHz       | known-good value, ~96% of wire-speed      |
| `TOUCH_CS`           | 17           | enables built-in XPT2046 driver           |

### Build and run

```bash
pio run -e shield_test -t upload
pio device monitor -b 115200 -e shield_test
```

The main radio firmware is unaffected — `pio run` on the default env
still builds `src/main.cpp` as before.

## Test phases

`setup()` runs six phases in sequence, each writing to both the screen
and Serial.

| # | Phase                 | Validates                                           |
|---|-----------------------|-----------------------------------------------------|
| 1 | color sweep           | SPI, COLMOD, CASET/RASET, inversion polarity        |
| 2 | text + primitives     | Adafruit-GFX API, font loading, drawing accuracy    |
| 3 | backlight PWM fade    | LEDC output on `TFT_BL`                             |
| 4 | fillScreen benchmark  | polled fast-path + DMA strip push (matched timing)  |
| 5 | pushSprite benchmark  | polled `pushPixels` vs async `pushPixelsDMA`        |
| 6 | animation benchmark   | single-buffer vs double-buffered DMA, per-strip     |

After the benchmarks the test runs (or reuses hard-coded) XPT2046
calibration, then enters a touch loop that draws a crosshair + trail
under the fingertip and prints raw coordinates to Serial.

### Touch calibration

On first boot `tft.calibrateTouch()` prompts for four corner taps and
prints a 5-value array to Serial. Paste those numbers into
`TOUCH_CALIBRATION` at the top of `test_shield.cpp` and re-flash to
skip the interactive step on subsequent boots.

Current calibration: `{ 477, 3203, 487, 3356, 6 }`.

## Benchmark results (measured on hardware)

```
polled  fillScreen x10     : 46.6 ms/fill     <- wire-speed bound
polled  drawString x100    : 1.83 ms/draw
DMA     fillFrame  x10     : 50.9 ms/fill     <- DMA setup adds ~4 ms
DMA+CPU fillFrame  x10     : 50.9 ms/fill + 267 k CPU ops during DMA
polled  pushPixels x10     : 6.1 ms/frame     <- measurement artifact, see below
DMA     pushPixelsDMA x10  : 50.7 ms/frame
animation single-buffer    : 9.3 FPS   (10.8 ms/strip total)
animation double-buffer    : 15.0 FPS  (6.7 ms/strip total)   speedup 1.62x
```

### What the numbers say

- `fillScreen` at 46.6 ms/frame is near the theoretical wire limit for
  240 * 320 * 16 bit at 27 MHz (45.5 ms). TFT_eSPI's polled fast path
  uses the SPI hardware's repeat-pattern feature and needs no RAM
  buffer, so it beats the DMA path (50.9 ms) for monotone fills —
  DMA has to fill a strip in RAM first and pay per-push setup cost.
- `DMA+CPU` counted ~267 000 cheap CPU loops during the ~500 ms of DMA
  transfer. That is the real benefit of DMA: the CPU is free while the
  SPI engine drains pixels.
- The polled `pushPixels` number (6.1 ms/frame) is a **measurement
  artifact**: TFT_eSPI's low-level writer queues bytes into the SPI
  hardware FIFO and the function returns before the wire has drained.
  The following `initDMA()` call picks up the state. The DMA number
  (50.7 ms) reflects the real wire-bound throughput.
- The **animation benchmark** is the honest DMA demo. Each strip does
  heavy per-pixel CPU work (32 LCG + XOR rounds) so render time
  (~6 ms/strip) is comparable to wire time (~4.5 ms/strip). Single-
  buffer runs them serially, double-buffer overlaps them, and the
  measured 1.62x speedup matches the theoretical
  `max(cpu, wire) / (cpu + wire)` bound.

## Guidance for the radio firmware

Based on these numbers, when the shield is integrated into the main
firmware:

| UI workload                         | Recommended path           |
|-------------------------------------|----------------------------|
| `fillScreen`, `fillRect`, solid bars| polled — fastest, simplest |
| Text, icons, band/volume labels     | polled — 1.8 ms/draw is ample for a UI |
| Frequency display, RSSI, static UI  | polled — no animation work |
| Spectrum analyzer / VU meter        | double-buffered DMA on 240x32 strips; gives ~1.6-2x FPS |
| Full-screen bitmap / splash         | `pushPixelsDMA` with DMA-capable buffer |

DMA is not a free win — it adds two DMA-capable strip buffers
(~15 KB each) and pipeline bookkeeping. Only pay that cost where
double-buffering visibly matters, i.e. an animated spectrum view.

## Files

| File                              | Role                                   |
|-----------------------------------|----------------------------------------|
| `src/test_shield.cpp`             | the test program                       |
| `include/User_Setup.h`            | TFT_eSPI pin + driver configuration    |
| `platformio.ini` (`shield_test`)  | dedicated build environment            |
| `src/test_display.cpp`            | earlier raw-SPI diagnostic, kept for reference |
