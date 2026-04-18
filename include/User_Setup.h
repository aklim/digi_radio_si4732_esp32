// ============================================================================
// User_Setup.h — TFT_eSPI configuration for the Waveshare 2.8" Rev 2.1
// (ST7789V, 240x320 portrait, resistive touch XPT2046) on an ESP32 DevKit v1.
//
// Force-included via platformio.ini for the `shield_test` environment:
//     build_flags = -DUSER_SETUP_LOADED=1
//                   -include "${PROJECT_DIR}/include/User_Setup.h"
// The main radio firmware (env:esp32dev) does NOT use TFT_eSPI.
//
// Design notes:
//
//   USE_HSPI_PORT
//     SCK=14 and MOSI=13 are HSPI's native IOMUX pins on the ESP32, so
//     forcing TFT_eSPI to use HSPI lets those critical signals bypass the
//     GPIO matrix (direct-connect, best signal integrity at 27 MHz).
//     MISO is remapped to GPIO 27 — harmless for the display (write-only)
//     and shared with the XPT2046 which does need read.
//
//   TFT_SPI_MODE SPI_MODE3
//     ST7789V on this shield latches on falling edge; mode 3 was the
//     known-good setting from the Adafruit_ST7789 bring-up.
//
//   TFT_INVERSION_ON
//     ST7789V panels on this shield ship with inverted polarity — without
//     this define the image comes out as a photographic negative.
//
//   TFT_RGB_ORDER TFT_BGR
//     Rev 2.1 wants BGR. If R and B look swapped, flip to TFT_RGB.
//
//   SPI_FREQUENCY 27 MHz
//     Value confirmed working on this exact shield+ESP32 combo in
//     Bodmer/TFT_eSPI discussion #3575. The panel spec allows up to 62.5
//     MHz, but 27 MHz is a safe fast setting that tolerates breadboard
//     wiring.
//
//   TOUCH_CS 17
//     Enables TFT_eSPI's built-in XPT2046 driver. No IRQ-pin support in
//     the library — TP_IRQ can stay unconnected. Calibration is required
//     once; see tft.calibrateTouch() in test_shield.cpp.
// ============================================================================

#ifndef USER_SETUP_H
#define USER_SETUP_H

// --- Driver & panel geometry ------------------------------------------------
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

// --- SPI port selection -----------------------------------------------------
#define USE_HSPI_PORT

// --- Display pin mapping ----------------------------------------------------
#define TFT_MISO 27     // remapped; shared with XPT2046 read path
#define TFT_MOSI 13     // HSPI IOMUX direct
#define TFT_SCLK 14     // HSPI IOMUX direct
#define TFT_CS   15     // HSPI native CS; strapping-safe
#define TFT_DC    2
#define TFT_RST  33
#define TFT_BL    4
#define TFT_BACKLIGHT_ON HIGH

// --- Touch (XPT2046) --------------------------------------------------------
#define TOUCH_CS 17

// --- SPI clocks & mode ------------------------------------------------------
#define TFT_SPI_MODE SPI_MODE3
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY   2000000
#define SPI_TOUCH_FREQUENCY  2500000

// --- Fonts ------------------------------------------------------------------
// Legacy bitmap fonts (FONT2/4/6/7) stay enabled as fallbacks during the
// FreeFonts rollout. LOAD_GFXFF enables TFT_eSPI's Adafruit-GFX-format free
// fonts (FreeSans, FreeSansBold, FreeMono, FreeSerif, ...) that the TFT UI
// now uses for all labels and the frequency display.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_GFXFF
#define SMOOTH_FONT

#endif  // USER_SETUP_H
