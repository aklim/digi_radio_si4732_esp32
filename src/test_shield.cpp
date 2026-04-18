// ============================================================================
// test_shield.cpp — Waveshare 2.8" TFT Touch Shield Rev 2.1 bring-up test
//                   (TFT_eSPI edition)
//
// Panel:  ST7789V, 240 x 320, SPI mode 3, BGR + inversion ON, 27 MHz
// Touch:  XPT2046, driven through TFT_eSPI's built-in driver (no IRQ pin)
// Board:  Bare ESP32 DevKit V1
//
// All pin/driver configuration lives in include/User_Setup.h and is
// force-included by platformio.ini for this env. Keep this file
// free of pin numbers — change them there, not here.
//
// ---- WIRING (shield label -> ESP32 GPIO) -----------------------------------
//   SCLK    -> GPIO 14       MOSI   -> GPIO 13      MISO   -> GPIO 27
//   LCD_CS  -> GPIO 15       LCD_DC -> GPIO 2       LCD_BL -> GPIO 4 (PWM)
//   LCD_RST -> GPIO 33       TP_CS  -> GPIO 17      TP_IRQ -> unused by TFT_eSPI
//   5V      -> ESP32 5V/VIN  GND    -> GND (shared, mandatory)
//   SD_CS   -> floating      (SD not tested here)
//
// ---- PREFLIGHT CHECKLIST ---------------------------------------------------
//   [ ] SB1/SB2/SB3 solder jumpers on the shield are bridged.
//   [ ] Shared GND between ESP32 and shield.
//   [ ] Shield 5V supplied from ESP32 5V/VIN.
//   [ ] MISO/MOSI not swapped.
//
// ---- BUILD & RUN -----------------------------------------------------------
//   pio run -e shield_test -t upload
//   pio device monitor -b 115200 -e shield_test
//
// ---- TOUCH CALIBRATION -----------------------------------------------------
// On first boot the test runs tft.calibrateTouch() — you must tap each of
// the four corner arrows in sequence. The resulting 5-uint16 calibration
// array is printed to Serial; paste it into TOUCH_CALIBRATION below and
// re-flash to skip the calibration on subsequent boots.
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

// ----- Hard-coded touch calibration -----------------------------------------
// Fill this with the 5 numbers printed by calibrateTouch() to skip the
// interactive calibration. Leave as {0,0,0,0,0} to force calibration every
// boot (useful during bring-up).
static uint16_t TOUCH_CALIBRATION[5] = { 477, 3203, 487, 3356, 6 };

// Backlight PWM (reuses LEDC pattern from main.cpp).
constexpr uint8_t  BL_LEDC_CHANNEL  = 0;
constexpr uint32_t BL_LEDC_FREQ_HZ  = 5000;
constexpr uint8_t  BL_LEDC_RES_BITS = 8;

// Tail-trail length for touch visualisation.
constexpr uint8_t TRAIL_LEN = 8;

// ----- Globals --------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();

struct TrailPoint { int16_t x; int16_t y; bool valid; };
static TrailPoint trail[TRAIL_LEN] = {};
static uint8_t    trailHead = 0;

// ----- Helpers --------------------------------------------------------------
static void setBacklight(uint8_t duty) {
    ledcWrite(BL_LEDC_CHANNEL, duty);
}

static void bannerSerial() {
    Serial.println();
    Serial.println(F("================================================="));
    Serial.println(F(" Waveshare 2.8\" TFT Touch Shield Rev 2.1"));
    Serial.println(F(" TFT_eSPI bring-up test"));
    Serial.println(F("================================================="));
    Serial.printf ("  panel    : %d x %d, SPI mode 3, 27 MHz\n",
                   TFT_WIDTH, TFT_HEIGHT);
    Serial.println(F("  pins     : see include/User_Setup.h"));
    Serial.println(F("  preflight: SB1/SB2/SB3 bridged, 5V + GND shared"));
    Serial.println();
}

static void phase1_colorSweep() {
    Serial.println(F("[phase1] full-screen color sweep"));
    const struct { uint16_t c; const char* name; } sweep[] = {
        { TFT_RED,   "RED"   },
        { TFT_GREEN, "GREEN" },
        { TFT_BLUE,  "BLUE"  },
        { TFT_WHITE, "WHITE" },
        { TFT_BLACK, "BLACK" },
    };
    for (const auto& s : sweep) {
        Serial.printf("  fill %s\n", s.name);
        tft.fillScreen(s.c);
        delay(800);
    }
}

static void phase2_primitives() {
    Serial.println(F("[phase2] text + primitives"));
    tft.fillScreen(TFT_BLACK);

    // Title banner
    tft.fillRect(0, 0, TFT_WIDTH, 32, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("TFT_eSPI OK", 6, 6, 4);

    // Color swatch grid (8 swatches, 2 rows x 4 cols)
    const uint16_t swatches[8] = {
        TFT_RED, TFT_GREEN, TFT_BLUE,    TFT_WHITE,
        TFT_YELLOW, TFT_CYAN, TFT_MAGENTA, TFT_DARKGREY
    };
    const int16_t swW = TFT_WIDTH / 4;
    const int16_t swH = 36;
    const int16_t swY0 = 40;
    for (int i = 0; i < 8; i++) {
        int16_t col = i % 4;
        int16_t row = i / 4;
        tft.fillRect(col * swW, swY0 + row * swH, swW - 2, swH - 2, swatches[i]);
    }

    // Font showcase — GFX built-in + TFT_eSPI numbered fonts
    int16_t y = swY0 + 2 * swH + 10;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Font 2:  Hello, world", 4, y,       2);
    tft.drawString("Font 4:  240x320",      4, y + 22,  4);
    tft.drawString("ST7789V", 4, y + 52, 6);

    // Primitives across the lower half
    const int16_t gy = y + 104;
    tft.drawLine(0, gy, TFT_WIDTH - 1, gy + 60, TFT_RED);
    tft.drawLine(0, gy + 60, TFT_WIDTH - 1, gy, TFT_GREEN);
    tft.drawRect(10, gy + 10, 60, 40, TFT_YELLOW);
    tft.fillRect(80, gy + 10, 60, 40, TFT_MAGENTA);
    tft.drawCircle(180, gy + 30, 22, TFT_CYAN);
    tft.fillCircle(220, gy + 30, 12, TFT_WHITE);

    delay(2500);
}

static void phase3_backlightFade() {
    Serial.println(F("[phase3] backlight PWM fade"));
    for (int d = 255; d >= 10; d -= 5)  { setBacklight(d); delay(12); }
    for (int d = 10;  d <= 255; d += 5) { setBacklight(d); delay(12); }
    setBacklight(220);
}

// --- DMA strip bench --------------------------------------------------------
// A full-screen 240x320x16 sprite would need 153600 bytes in a single
// contiguous DMA-capable block — more than the ESP32 WROOM-32 internal
// heap can usually serve. We instead push the frame as 10 horizontal
// strips of 240x32 pixels (15360 bytes each), which fits any ESP32 and
// is also how real-world code uses DMA (one buffer, many pushes).
constexpr int16_t STRIP_H = 32;
constexpr int     STRIPS_PER_FRAME = TFT_HEIGHT / STRIP_H;   // 320 / 32 = 10
constexpr size_t  STRIP_PIXELS = TFT_WIDTH * STRIP_H;

// Polled baseline — CPU blocked inside SPI writes for the whole run.
static uint32_t benchFillPolled(uint8_t iterations) {
    uint32_t t0 = micros();
    for (int i = 0; i < iterations; i++) {
        tft.fillScreen(i & 1 ? TFT_BLUE : TFT_BLACK);
    }
    return micros() - t0;
}

// Fill a strip buffer with a single colour, byte-swapped to the order
// that pushPixelsDMA expects.
static void fillStrip(uint16_t* buf, uint16_t color) {
    // tft_color565 stores as native; pushPixelsDMA handles byte order
    // via setSwapBytes() — we set swap true before the first push.
    for (size_t i = 0; i < STRIP_PIXELS; i++) buf[i] = color;
}

static uint32_t benchFillDMA(uint16_t* strip, uint8_t iterations) {
    uint32_t t0 = micros();
    for (int i = 0; i < iterations; i++) {
        fillStrip(strip, i & 1 ? TFT_MAGENTA : TFT_BLACK);
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        for (int s = 0; s < STRIPS_PER_FRAME; s++) {
            tft.pushPixelsDMA(strip, STRIP_PIXELS);
        }
        tft.dmaWait();
        tft.endWrite();
    }
    return micros() - t0;
}

// Same work but instead of the implicit wait inside the next
// pushPixelsDMA call, we explicitly spin on dmaBusy() and count how
// many CPU operations we can squeeze in — this is the actual benefit
// of DMA: your CPU is free to compute while SPI is transferring.
static uint32_t benchFillDMAWithCPU(uint16_t* strip,
                                    uint8_t iterations,
                                    uint64_t& cpuOpsOut) {
    uint64_t ops = 0;
    volatile uint32_t acc = 1;
    uint32_t t0 = micros();
    for (int i = 0; i < iterations; i++) {
        fillStrip(strip, i & 1 ? TFT_GREEN : TFT_BLACK);
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        for (int s = 0; s < STRIPS_PER_FRAME; s++) {
            tft.pushPixelsDMA(strip, STRIP_PIXELS);
            while (tft.dmaBusy()) {
                acc = acc * 1103515245u + 12345u;
                ops++;
            }
        }
        tft.endWrite();
    }
    uint32_t elapsed = micros() - t0;
    cpuOpsOut = ops;
    (void)acc;
    return elapsed;
}

static void phase4_benchmark() {
    Serial.println(F("[phase4] benchmark — polled vs DMA"));
    Serial.printf ("  free heap before: %u bytes\n", ESP.getFreeHeap());
    constexpr uint8_t N = 10;

    // --- 4a. Polled baseline ---
    uint32_t polledUs = benchFillPolled(N);
    Serial.printf("  polled  fillScreen x%u : %lu us  (%.1f ms/fill)\n",
                  N, polledUs, polledUs / (float)(N * 1000));

    // --- 4b. drawString throughput (polled) ---
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    uint32_t t0 = micros();
    for (int i = 0; i < 100; i++) {
        tft.drawString("Hello TFT_eSPI", 10, 20 + (i % 10) * 14, 2);
    }
    uint32_t textUs = micros() - t0;
    Serial.printf("  polled  drawString x100: %lu us  (%.1f us/draw)\n",
                  textUs, textUs / 100.0);

    // --- 4c. DMA strip push ---
    // DMA needs memory in internal SRAM (MALLOC_CAP_DMA), not PSRAM.
    uint16_t* strip = (uint16_t*)heap_caps_malloc(
        STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);

    uint32_t dmaUs = 0;
    uint32_t dmaCpuUs = 0;
    uint64_t cpuOps = 0;

    if (!strip) {
        Serial.println(F("  DMA bench skipped — strip buffer allocation failed"));
    } else {
        tft.initDMA();
        tft.setSwapBytes(true);   // pushPixelsDMA wants RGB565 little-endian

        dmaUs    = benchFillDMA(strip, N);
        dmaCpuUs = benchFillDMAWithCPU(strip, N, cpuOps);

        tft.setSwapBytes(false);
        tft.deInitDMA();
        heap_caps_free(strip);

        Serial.printf("  DMA     fillFrame  x%u : %lu us  (%.1f ms/fill)\n",
                      N, dmaUs, dmaUs / (float)(N * 1000));
        Serial.printf("  DMA+CPU fillFrame  x%u : %lu us  (%.1f ms/fill, "
                      "%llu CPU ops during DMA)\n",
                      N, dmaCpuUs, dmaCpuUs / (float)(N * 1000), cpuOps);
    }

    // --- Results on screen -------------------------------------------------
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("Benchmark (x10 fills)", 10, 10, 4);

    char line[48];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(line, sizeof(line), "polled : %lu ms", polledUs / 1000);
    tft.drawString(line, 10, 60, 4);

    if (dmaUs) {
        snprintf(line, sizeof(line), "DMA    : %lu ms", dmaUs / 1000);
        tft.drawString(line, 10, 95, 4);

        snprintf(line, sizeof(line), "DMA+CPU: %lu ms", dmaCpuUs / 1000);
        tft.drawString(line, 10, 130, 4);

        snprintf(line, sizeof(line), "CPU ops during DMA: %llu", cpuOps);
        tft.drawString(line, 10, 175, 2);
    } else {
        tft.drawString("DMA unavailable", 10, 95, 4);
    }

    snprintf(line, sizeof(line), "drawString x100: %lu ms", textUs / 1000);
    tft.drawString(line, 10, 210, 2);

    delay(3500);
}

// ============================================================================
// Phase 5 — pushImage (polled) vs pushPixelsDMA (fair comparison)
//
// Unlike fillScreen, both paths here push the SAME pre-rendered strip of
// gradient pixels. That removes the polled-fillScreen fast path from the
// equation and gives us apples-to-apples: who moves RAM pixels to SPI
// faster. DMA should win here.
// ============================================================================

static void renderGradientStrip(uint16_t* buf) {
    for (int y = 0; y < STRIP_H; y++) {
        for (int x = 0; x < TFT_WIDTH; x++) {
            uint8_t r = (x * 255) / TFT_WIDTH;
            uint8_t g = (y * 255) / STRIP_H;
            uint8_t b = ((x + y * 8) * 255) / (TFT_WIDTH + STRIP_H * 8);
            buf[y * TFT_WIDTH + x] = tft.color565(r, g, b);
        }
    }
}

static void phase5_pushBench() {
    Serial.println(F("[phase5] pushImage polled vs pushPixelsDMA"));
    constexpr uint8_t N = 10;

    uint16_t* strip = (uint16_t*)heap_caps_malloc(
        STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!strip) {
        Serial.println(F("  skipped — strip alloc failed"));
        return;
    }
    renderGradientStrip(strip);

    tft.setSwapBytes(true);   // color565 values need byte-swap on the wire

    // Polled: explicit low-level pushPixels (truly synchronous). We use
    // the same startWrite + setAddrWindow + loop + endWrite shape as the
    // DMA path below so the two paths differ only in sync vs async push.
    uint32_t t0 = micros();
    for (int i = 0; i < N; i++) {
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        for (int s = 0; s < STRIPS_PER_FRAME; s++) {
            tft.pushPixels(strip, STRIP_PIXELS);
        }
        tft.endWrite();
    }
    uint32_t polledUs = micros() - t0;

    // DMA: identical shape, async pushes, wait once at end.
    tft.initDMA();
    t0 = micros();
    for (int i = 0; i < N; i++) {
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        for (int s = 0; s < STRIPS_PER_FRAME; s++) {
            tft.pushPixelsDMA(strip, STRIP_PIXELS);
        }
        tft.dmaWait();
        tft.endWrite();
    }
    uint32_t dmaUs = micros() - t0;

    tft.deInitDMA();
    tft.setSwapBytes(false);
    heap_caps_free(strip);

    float speedup = (float)polledUs / (float)dmaUs;
    Serial.printf("  polled  pushImage    x%u : %lu us  (%.1f ms/frame)\n",
                  N, polledUs, polledUs / (float)(N * 1000));
    Serial.printf("  DMA     pushPixelsDMAx%u : %lu us  (%.1f ms/frame)\n",
                  N, dmaUs, dmaUs / (float)(N * 1000));
    Serial.printf("  DMA speedup: %.2fx\n", speedup);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("pushSprite bench", 10, 10, 4);
    char line[48];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "polled: %lu ms / %u frames", polledUs / 1000, N);
    tft.drawString(line, 10, 60, 4);
    snprintf(line, sizeof(line), "DMA   : %lu ms / %u frames", dmaUs / 1000, N);
    tft.drawString(line, 10, 90, 4);
    snprintf(line, sizeof(line), "speedup: %.2fx", speedup);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString(line, 10, 120, 6);
    delay(3000);
}

// ============================================================================
// Phase 6 — double-buffered animation on strips
//
// Real-world DMA scenario: each strip of every frame is computed from
// scratch (scrolling procedural gradient), so there is meaningful CPU work
// per strip. Two versions:
//
//   single-buffer:  render -> push -> WAIT -> render next ...    (serial)
//   double-buffer:  render A | push A + render B | push B + render A ...
//                                                        (CPU overlaps wire)
//
// With CPU work and wire time roughly balanced, double-buffering should
// approach max(CPU, wire) per strip instead of (CPU + wire).
// ============================================================================

static inline uint16_t procPixel(int x, int y) {
    // Per-pixel CPU work intentionally sized to match ~SPI wire time
    // per strip. 32 LCG+XOR rounds is ~150 cycles on an Xtensa core,
    // which at 7680 pixels/strip gives ~4-5 ms of CPU render time,
    // roughly equal to the DMA transfer time. This is the regime where
    // double-buffering actually pays off — render(B) truly overlaps
    // transfer(A) instead of finishing almost instantly.
    uint32_t v = (uint32_t)x * 31u + (uint32_t)y * 17u;
    for (int i = 0; i < 32; i++) {
        v = v * 1103515245u + 12345u;
        v ^= (v >> 7);
    }
    uint8_t r = v;
    uint8_t g = v >> 8;
    uint8_t b = v >> 16;
    return ((uint16_t)(r >> 3) << 11) |
           ((uint16_t)(g >> 2) << 5)  |
           ((uint16_t)(b >> 3));
}

static void renderAnimStrip(uint16_t* buf, int stripY, int scroll) {
    for (int row = 0; row < STRIP_H; row++) {
        int worldY = stripY + row + scroll;
        uint16_t* line = &buf[row * TFT_WIDTH];
        for (int col = 0; col < TFT_WIDTH; col++) {
            line[col] = procPixel(col, worldY);
        }
    }
}

static uint32_t animSingleBuffer(uint16_t* buf, int frames) {
    uint32_t t0 = micros();
    for (int f = 0; f < frames; f++) {
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        for (int s = 0; s < STRIPS_PER_FRAME; s++) {
            renderAnimStrip(buf, s * STRIP_H, f);
            tft.pushPixelsDMA(buf, STRIP_PIXELS);
            tft.dmaWait();   // must block — buffer reused immediately
        }
        tft.endWrite();
    }
    return micros() - t0;
}

static uint32_t animDoubleBuffer(uint16_t* bufA, uint16_t* bufB, int frames) {
    uint32_t t0 = micros();
    for (int f = 0; f < frames; f++) {
        tft.startWrite();
        tft.setAddrWindow(0, 0, TFT_WIDTH, TFT_HEIGHT);
        uint16_t* cur = bufA;
        uint16_t* nxt = bufB;
        // Prime the pipeline: render strip 0 into cur, start DMA.
        renderAnimStrip(cur, 0, f);
        tft.pushPixelsDMA(cur, STRIP_PIXELS);
        // For strips 1..9: while DMA pushes `cur`, render into `nxt`.
        for (int s = 1; s < STRIPS_PER_FRAME; s++) {
            renderAnimStrip(nxt, s * STRIP_H, f);
            tft.dmaWait();                    // usually already done by now
            tft.pushPixelsDMA(nxt, STRIP_PIXELS);
            uint16_t* tmp = cur; cur = nxt; nxt = tmp;
        }
        tft.dmaWait();
        tft.endWrite();
    }
    return micros() - t0;
}

static void phase6_animBench() {
    Serial.println(F("[phase6] double-buffered animation"));
    constexpr int FRAMES = 60;

    uint16_t* bufA = (uint16_t*)heap_caps_malloc(
        STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    uint16_t* bufB = (uint16_t*)heap_caps_malloc(
        STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!bufA || !bufB) {
        Serial.println(F("  skipped — buffer alloc failed"));
        if (bufA) heap_caps_free(bufA);
        if (bufB) heap_caps_free(bufB);
        return;
    }

    tft.initDMA();
    tft.setSwapBytes(true);

    uint32_t singleUs = animSingleBuffer(bufA, FRAMES);
    uint32_t doubleUs = animDoubleBuffer(bufA, bufB, FRAMES);

    tft.setSwapBytes(false);
    tft.deInitDMA();
    heap_caps_free(bufA);
    heap_caps_free(bufB);

    float singleFps = (float)FRAMES * 1e6f / (float)singleUs;
    float doubleFps = (float)FRAMES * 1e6f / (float)doubleUs;
    float speedup   = doubleFps / singleFps;

    Serial.printf("  single-buffer : %lu us / %d frames = %.1f FPS\n",
                  singleUs, FRAMES, singleFps);
    Serial.printf("  double-buffer : %lu us / %d frames = %.1f FPS\n",
                  doubleUs, FRAMES, doubleFps);
    Serial.printf("  speedup: %.2fx\n", speedup);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("Animation bench", 10, 10, 4);
    char line[48];
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "single-buf: %.1f FPS", singleFps);
    tft.drawString(line, 10, 60, 4);
    snprintf(line, sizeof(line), "double-buf: %.1f FPS", doubleFps);
    tft.drawString(line, 10, 100, 4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(line, sizeof(line), "speedup: %.2fx", speedup);
    tft.drawString(line, 10, 150, 6);
    delay(3000);
}

// ----- Touch ----------------------------------------------------------------
static bool calibrationLooksValid() {
    for (int i = 0; i < 5; i++) if (TOUCH_CALIBRATION[i] != 0) return true;
    return false;
}

static void runCalibrationIfNeeded() {
    if (calibrationLooksValid()) {
        tft.setTouch(TOUCH_CALIBRATION);
        Serial.println(F("[touch] using hard-coded calibration"));
        return;
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Touch each arrow", 20, 10, 4);
    tft.drawString("precisely with a stylus", 20, 50, 2);

    uint16_t calData[5];
    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    Serial.println(F("[touch] calibration complete — paste these 5 numbers"));
    Serial.println(F("[touch] into TOUCH_CALIBRATION[] at the top of test_shield.cpp"));
    Serial.print  (F("  { "));
    for (int i = 0; i < 5; i++) {
        Serial.print(calData[i]);
        if (i < 4) Serial.print(F(", "));
    }
    Serial.println(F(" }"));

    // Keep using the fresh calibration for this session.
    tft.setTouch(calData);
}

static void drawTouchHUD() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, TFT_WIDTH, 28, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawString("Touch test", 6, 6, 4);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Tap anywhere - crosshair follows.", 4, 36, 2);
}

static void drawCrosshair(int16_t x, int16_t y, uint16_t color) {
    tft.drawFastHLine(x - 6, y, 13, color);
    tft.drawFastVLine(x, y - 6, 13, color);
    tft.drawCircle(x, y, 3, color);
}

static void pushTrail(int16_t x, int16_t y) {
    TrailPoint& slot = trail[trailHead];
    if (slot.valid) drawCrosshair(slot.x, slot.y, TFT_BLACK);
    slot = { x, y, true };
    trailHead = (trailHead + 1) % TRAIL_LEN;
    drawCrosshair(x, y, TFT_YELLOW);
}

// ----- Arduino entry points -------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    bannerSerial();

    // Backlight PWM first — visible feedback even if SPI fails.
    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ_HZ, BL_LEDC_RES_BITS);
    ledcAttachPin(TFT_BL, BL_LEDC_CHANNEL);
    setBacklight(220);

    Serial.println(F("[init] tft.init() with User_Setup.h"));
    tft.init();
    tft.setRotation(0);              // portrait 240x320
    tft.fillScreen(TFT_BLACK);

    phase1_colorSweep();
    phase2_primitives();
    phase3_backlightFade();
    phase4_benchmark();
    phase5_pushBench();
    phase6_animBench();

    runCalibrationIfNeeded();

    drawTouchHUD();
    Serial.println(F("[ready] touch the screen"));
}

void loop() {
    uint16_t tx, ty;
    if (tft.getTouch(&tx, &ty)) {
        Serial.printf("touch px=(%3u,%3u)\n", tx, ty);
        pushTrail((int16_t)tx, (int16_t)ty);
        delay(15);
    }
}
