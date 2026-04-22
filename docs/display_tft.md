# TFT Display

`env:esp32dev` drives a Waveshare 2.8" TFT Touch Shield Rev 2.1
(ST7789V 240×320 + XPT2046 resistive touch) via Bodmer's
[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library. This is the
only product display — the earlier SSD1315 OLED variant has been
retired.

The shield bring-up fixture (`env:shield_test`, `src/test_shield.cpp`) is a
separate environment used only for hardware validation and is not published
by the release workflow. See [display_shield_test.md](display_shield_test.md).

## Layout: ATS-Mini parity port

The on-screen UI is a 1:1 port of the
[ATS-Mini](https://github.com/esp32-si4732/ats-mini) receiver firmware. The
panel is driven in **landscape** (`setRotation(3)` → 320×240) and the widget
geometry, colour-theme struct, and 7-segment frequency font all match
upstream. File structure mirrors the reference project so diffs are direct:

| Our file | Upstream counterpart | Purpose |
|---|---|---|
| `include/Draw.h`          | `ats-mini/Draw.h`          | OFFSET_* macros + widget prototypes |
| `src/Draw.cpp`            | `ats-mini/Draw.cpp`        | drawBattery / drawBandAndMode / drawFrequency / drawStationName / drawSMeter / drawStereoIndicator / drawRadioText / drawScale / drawScanGraphs |
| `src/Layout-Default.cpp`  | `ats-mini/Layout-Default.cpp` | Per-frame orchestration |
| `src/Scan.cpp` + `include/Scan.h` | `ats-mini/Scan.cpp` | Bandscope sweep engine |
| `include/Themes.h`, `src/Themes.cpp` | `ats-mini/Themes.*` | 44-field `ColorTheme` + 9 presets |
| `src/Battery.cpp`         | `ats-mini/Battery.cpp`     | Placeholder (4.15 V / 80 %) until LiPo hardware lands |

Rendering pipeline: `main.cpp::updateDisplay()` → `drawLayoutDefault()`
(sprite-backed, 8 bpp, 320×240 ≈ 77 KB heap; falls back to per-widget
rect-clears on alloc failure).

## Orientation and touch

Landscape only — the former `DISPLAY_FLIPPED` portrait knob retired when
the layout moved to ATS-Mini geometry. `tft.setRotation(3)` runs in
`initDisplay()`. Our XPT2046 calibration was captured in rotation 0
(240×320 portrait), but TFT_eSPI's `getTouch()` scales the ADC output
to the *current* rotation's `_tft_data.width / height` without re-running
the calibration. The net effect: raw ADC values come back mapped into
a 320×240 frame whose axes are still aligned to the portrait ADC map,
so both the swap **and** a 4/3 vs 3/4 stretch are needed before the
coords match what's drawn on screen. `handleTouch()` in `main.cpp`
applies:

    landscape_x = 319 - (4 × rawY) / 3
    landscape_y = (3 × rawX) / 4

Verified empirically by logging raw + mapped coords and tapping each
of the five on-screen buttons in the bottom transport row.

## Build and flash

```bash
pio run -e esp32dev                   # build
pio run -e esp32dev -t upload         # build + flash
pio device monitor -b 115200 -e esp32dev
```

On first boot the splash screen shows the firmware version (from
`git describe --tags --dirty`) and the panel clears to the main UI within
~1 second. There is no separate touch calibration step — the calibration
constants are pinned from the values that `tft.calibrateTouch()` produced
during the shield bring-up.

### Physical orientation

Landscape-only. See the "Orientation and touch" section above.

## Pin map

Si4732 / encoder pins are documented in
[hardware.md](hardware.md); the TFT shield uses HSPI, which is
independent of the Si4732 I²C bus.

| Signal       | ESP32 GPIO | Consumer      |
|--------------|------------|---------------|
| I2C SDA      | 21         | Si4732        |
| I2C SCL      | 22         | Si4732        |
| Encoder A    | 18         | Rotary        |
| Encoder B    | 19         | Rotary        |
| Encoder BTN  |  5         | Rotary        |
| TFT MOSI     | 13         | HSPI (IOMUX)  |
| TFT SCLK     | 14         | HSPI (IOMUX)  |
| TFT MISO     | 27         | HSPI (shared) |
| TFT CS       | 15         | HSPI          |
| TFT DC       |  2         | —             |
| TFT RST      | 33         | —             |
| TFT BL       |  4         | LEDC PWM      |
| Touch CS     | 17         | XPT2046       |

Driver configuration (ST7789V, BGR, inversion on, HSPI, 27 MHz) is in
[include/User_Setup.h](../include/User_Setup.h). Shield wiring quirks (SB1/SB2/SB3
jumpers, 5V + GND shared) are in [hardware.md](hardware.md).

## UI layout

Portrait, 240×320, rotation 0. Coordinates and color/font choices are
`constexpr` in [include/ui_layout.h](../include/ui_layout.h).

```
+--------------------------------------+  y=0
| FM  STEREO                     v1.x  |  header  (h=28, bg NAVY)
+--------------------------------------+  y=32
|                                      |
|         1 0 2 . 4   MHz              |  freq zone (h=108, FSSB24)
|                                      |
+--------------------------------------+  y=140
| PS: RadioXYZ                         |
| lorem ipsum dolor sit amet...        |  RDS zone  (h=68)
+--------------------------------------+  y=208
| RSSI   ◡◜ ◝◠◞ ◠◟◠ ◝◠◞ ◠◜◝◠◞   48 dB |
| SNR  15 dB                stereo ●   |  meter zone (h=48, needle gauge)
+--------------------------------------+  y=256
| Vol   [########-----------] 30       |  volume zone (h=48)
+--------------------------------------+  y=304
| v1.x.y  USB  102.4 MHz               |  footer (h=16, FSS9)
+--------------------------------------+  y=320
```

### Fonts

All typography renders through TFT_eSPI's Adafruit-GFX free fonts
(built in when `LOAD_GFXFF` is defined in `include/User_Setup.h`). No
vendored font headers — the linker drops any free font that is not
actually referenced, so the Flash cost is bounded to the 4 faces below.

| Constant       | Free font                | Use                               |
|----------------|--------------------------|-----------------------------------|
| `FREQ_FONT`    | `FreeSansBold24pt7b`     | Frequency digits + MHz label      |
| `HEADER_FONT`  | `FreeSansBold12pt7b`     | "FM", STEREO / MONO, "Vol", "PS:" |
| `LABEL_FONT`   | `FreeSans9pt7b`          | RSSI / SNR labels, footer, RDS RT |
| `VALUE_FONT`   | `FreeSansBold9pt7b`      | Numeric dBµV next to meter label  |

Legacy bitmap-font aliases (`FONT_BIG=7`, `FONT_LABEL=4`, `FONT_SMALL=2`)
are still defined in `include/ui_layout.h` as fallbacks and will be
removed once the GFX rollout has been in master for a release.

### Focus border

The active zone (frequency or volume, depending on which one the encoder
currently drives) gets a 1-pixel `TFT_YELLOW` border; the inactive zone gets
a dim grey border. Toggling mode repaints only those two borders.

### S-meter

RSSI (0..127 dBµV from `radio.getCurrentRSSI()`) is clamped to the
0..60 dBµV range and painted as an analog needle gauge in the left
portion of the meter zone. Geometry comes from `GAUGE_*` constants in
`include/ui_layout.h`: the pivot lives below the visible sprite so
only the top fan of an imaginary larger dial shows, giving the classic
moving-coil silhouette. Tick marks sit at every 10 dBµV along the arc;
the needle is green below 20 dBµV, yellow up to 45, and red above that.

The gauge is rendered through a `TFT_eSprite` (`GAUGE_W × GAUGE_H`
pixels) and pushed in one blit so the needle moves without flicker.
Between radio polls the needle is interpolated client-side by
`pumpNeedleAnimation()` in [src/main.cpp](../src/main.cpp), which
runs an EMA on `radioGetRssi()` every `NEEDLE_ANIM_MS` (30 ms) and
repaints only when the smoothed value has moved more than
`NEEDLE_EPSILON`. Once the needle settles the gauge is idle — zero CPU
cost at rest.

The numeric dBµV value stays on the right side of the zone; SNR and
stereo dot sit on the second row, unchanged. The stereo dot (green when
`radio.getCurrentPilot()` is true, dim grey otherwise) is mirrored in
the header "STEREO / MONO" label.

## Input

### Rotary encoder (primary)

- **Rotate** — tune within the current band (in FREQ mode) or adjust
  volume (in VOL mode). Wraps at band edges; volume clamps at 0 and
  `MAX_VOLUME` (63).
- **Short-click** (<500 ms) — toggle between FREQ and VOL.
- **Long-press** (≥500 ms) — open the menu (see
  [menu.md](menu.md)). Inside the menu, rotation navigates and click
  confirms; another long-press backs out.

### Touch zones

Tapping the screen runs two hit-test passes in `handleTouch()` (inside
`main.cpp`). Debounce is 200 ms; after a processed tap the loop sleeps
15 ms to let the resistive XPT2046 settle — the same pattern as
`test_shield.cpp`.

**Mode-toggle zones** (freq / volume strips):

| Tap target      | Rect (x, y, w, h)    | Action                 |
|-----------------|----------------------|------------------------|
| Frequency zone  | `(0, 32, 320, 108)`  | `MODE_FREQUENCY`       |
| Volume zone     | `(0, 256, 320, 48)`  | `MODE_VOLUME` *(off-panel in landscape — effectively dead code)* |

**Bottom transport-row buttons** (`y=180..230`, 58×50 each, 5 px gaps;
constants live in `include/Draw.h` so drawn geometry and hit rects
can't drift):

| Icon | Label            | Rect (x, y, w, h)      | Action |
|------|------------------|------------------------|--------|
| `⏪` | Seek Down        | `(5,   180, 58, 50)`   | Start auto-seek downward |
| `◀` | Prev Preset      | `(68,  180, 58, 50)`   | Jump to previous memory preset in current band (by freq) |
| `🔊` | Mute             | `(131, 180, 58, 50)`   | Toggle `radioSetMute` user-latched mute |
| `▶` | Next Preset      | `(194, 180, 58, 50)`   | Jump to next memory preset in current band (by freq) |
| `⏩` | Seek Up          | `(257, 180, 58, 50)`   | Start auto-seek upward |

All icons are drawn procedurally from filled triangles + rectangles in
`drawButtonRow()` (no bitmap fonts, so the glyphs rescale with the
theme fill colours). The row is hidden while a bandscope sweep is
running — the sweep's graph extends down into the button-row's Y range
and the two modes are mutually exclusive (both go through
`radioScanEnter / radioScanExit`). See
[src/Draw.cpp](../src/Draw.cpp) for the icon geometry and
[src/Seek.cpp](../src/Seek.cpp) for the auto-seek state machine
(peak-climb included so the engine lands on the carrier rather than
its adjacent-channel skirt).

Touch calibration `{ 477, 3203, 487, 3356, 6 }` is hard-coded in
`main.cpp`. If the shield is replaced, re-run the calibration phase
in `src/test_shield.cpp` and paste the resulting 5 numbers. The
landscape remap (see "Orientation and touch" above) absorbs the
rotation mismatch so no calibration change is needed when adding new
hit rects at any Y position.

## RDS behaviour

Enabled inside `radioInit()` via `setRdsConfig(1, 3, 3, 3, 3)` (permissive
block-error thresholds — more text at the cost of occasional garbled
characters, which is the right tradeoff for a portable receiver).

`radio.cpp` polls the chip at most every 200 ms. When `getRdsSync()` is true
and `getRdsReceived()` signals new data, the library's PS (Programme Service
name, up to 8 chars) and RT (RadioText, up to 64 chars) buffers are copied
into local mirror buffers and diffed against the previous values. The UI is
notified only when the text actually changes.

When sync has been lost for more than 10 seconds the mirrors are cleared and
the UI shows `PS: --` with an empty RadioText line. On tune the mirrors are
cleared immediately — the gating on `getRdsSync()` stops the library's
stale bytes from being copied over until the new station locks in.

RadioText scrolling is not implemented; the UI truncates RT to the 38
characters that fit on one line at the current label font
(`FreeSans9pt7b`). See
[future_improvements.md](future_improvements.md) for the marquee plan.

## Rendering pipeline

`updateDisplayTft()` in [src/main.cpp](../src/main.cpp) runs a
per-zone dirty-flags byte:

| Flag           | Set by                                              |
|----------------|-----------------------------------------------------|
| `DIRTY_HEADER` | boot, stereo pilot change                           |
| `DIRTY_FREQ`   | `radioSetFrequency()`, mode change (border)         |
| `DIRTY_RDS`    | `radioPollRds()` when PS or RT changed              |
| `DIRTY_METER`  | `radioPollSignal()` when RSSI / SNR / pilot changed |
| `DIRTY_VOL`    | volume change, mode change (border)                 |
| `DIRTY_FOOTER` | frequency change, boot                              |

On boot all flags are set so the first render paints the whole screen.
On subsequent iterations, only zones whose flags are set get
`fillRect(zone, COL_BG)` + a fresh `drawString()` / `drawRect()` pass.
The benchmark numbers from [display_shield_test.md](display_shield_test.md)
(1.83 µs/drawString, 46 ms polled full-fill) comfortably fit the 2-5 Hz
update rate we actually trigger.

Only the S-meter needle gauge renders through a `TFT_eSprite` — the
animated needle would otherwise flicker between its previous and new
positions. All other zones still use direct `fillRect` + `drawString`
because their update cadence (≤10 Hz) is slow enough not to flicker on
direct draws. Migrating the FREQ zone (and eventually all zones) to a
full sprite+DMA pipeline is tracked in
[future_improvements.md](future_improvements.md); the pattern is proven
in `test_shield.cpp` phase 5/6.

## Verification checklist

Flash the firmware, open the serial monitor, and confirm:

1. Splash at boot → main UI within ~1 s.
2. Rotate the encoder → frequency changes smoothly, no flicker on FREQ zone.
3. Click the encoder → focus border moves FREQ ↔ VOL; rotation adjusts the
   focused parameter.
4. Tap the FREQ zone on screen → focus to FREQ; tap the VOL zone → focus
   to VOL.
5. Tune a known strong local FM station — `PS:` text appears within ~3-5 s,
   RadioText within ~10 s.
6. Tune an empty frequency → PS/RT clear within 10 s, UI shows `PS: --`.
7. Stereo dot flips green / dim-grey as the pilot bit comes and goes.
8. RSSI needle and SNR number track the live signal — the needle should
   animate smoothly (not snap) between RSSI samples, with green→yellow→red
   colour grading as the signal rises.
9. Version string (top-right of header, left of footer) matches
   `git describe --tags --dirty` and `strings firmware.elf | grep FW=`.

## Known limits

- **Battery meter:** no hardware in v1; the footer prints the literal
  `"USB"` via `POWER_SOURCE` in `ui_layout.h`. Wiring a LiPo +
  voltage divider to an ADC pin + replacing the string with an
  `analogRead()` percentage is tracked in
  [future_improvements.md](future_improvements.md).
- **RDS RadioText scroll:** truncated to 38 chars; marquee scrolling is v2.
- **Single-FM-band:** `radio.cpp` wraps FM only. AM / SW / SSB can be added
  later without a UI rework.
- **No menu / presets / waterfall / brightness slider** — all v2; see the
  roadmap doc.
