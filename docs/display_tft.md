# TFT Display Variant

`env:esp32dev_tft` is the second product firmware in this repository. It drives
a Waveshare 2.8" TFT Touch Shield Rev 2.1 (ST7789V 240×320 + XPT2046 resistive
touch) via Bodmer's [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library.
The underlying Si4732 radio and rotary-encoder logic are shared with the OLED
firmware — they live in `src/radio.cpp` and `src/input.cpp`. Only the display
backend and the UI layout differ between the two variants.

The shield bring-up fixture (`env:shield_test`, `src/test_shield.cpp`) is a
separate environment used only for hardware validation and is not published
by the release workflow. See [display_shield_test.md](display_shield_test.md).

## Build and flash

```bash
pio run -e esp32dev_tft                   # build
pio run -e esp32dev_tft -t upload         # build + flash
pio device monitor -b 115200 -e esp32dev_tft
```

On first boot the splash screen shows the firmware version (from
`git describe --tags --dirty`) and the panel clears to the main UI within
~1 second. There is no separate touch calibration step — the calibration
constants are pinned from the values that `tft.calibrateTouch()` produced
during the shield bring-up.

### Physical orientation

`DISPLAY_FLIPPED` in [include/ui_layout_tft.h](../include/ui_layout_tft.h)
chooses the panel orientation. `true` rotates the UI 180° (useful when the
shield is mounted in a case that cannot be turned over); `false` is
right-side-up portrait. Defaults to `true` for the current enclosure. When
the flag is `true`, `main_tft.cpp` also mirrors touch coordinates
(`x → W-1-x`, `y → H-1-y`) so finger taps still hit the visually-correct
zone — the pinned touch calibration was captured in rotation 0 and
TFT_eSPI's `getTouch()` does not compensate for rotation on its own.

## Pin map

All pins are orthogonal to the OLED firmware; the two variants share the
board and only the physical display differs.

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
`constexpr` in [include/ui_layout_tft.h](../include/ui_layout_tft.h).

```
+--------------------------------------+  y=0
| FM  STEREO                     v1.x  |  header  (h=28, bg NAVY)
+--------------------------------------+  y=32
|                                      |
|        1 0 2 . 4       MHz           |  freq zone (h=108, FONT7)
|                                      |
+--------------------------------------+  y=140
| PS: RadioXYZ                         |
| lorem ipsum dolor sit amet...        |  RDS zone  (h=68)
+--------------------------------------+  y=208
| RSSI  [################-----] 48 dB  |
| SNR  15 dB                stereo ●   |  meter zone (h=48)
+--------------------------------------+  y=256
| Vol   [########-----------] 30       |  volume zone (h=48)
+--------------------------------------+  y=304
| v1.x.y  USB  102.4 MHz               |  footer (h=16, FONT2)
+--------------------------------------+  y=320
```

### Fonts

All three fonts come from TFT_eSPI itself (built in via `User_Setup.h`) —
no FreeFonts pack in v1.

| Constant      | Font # | Use                             |
|---------------|--------|---------------------------------|
| `FONT_BIG`    | 7      | Frequency (7-segment, numeric)  |
| `FONT_LABEL`  | 4      | Labels, volume & SNR numbers    |
| `FONT_SMALL`  | 2      | Footer, RDS RadioText body      |

### Focus border

The active zone (frequency or volume, depending on which one the encoder
currently drives) gets a 1-pixel `TFT_YELLOW` border; the inactive zone gets
a dim grey border. Toggling mode repaints only those two borders.

### S-meter

RSSI (0..127 dBµV from `radio.getCurrentRSSI()`) is clamped to the 0..60 dBµV
range and mapped to a 150-pixel bar. Tick marks sit every 10 dBµV above the
bar so the scale is readable at a glance. The numeric dBµV value is printed
to the right of the bar.

The stereo dot (green when `radio.getCurrentPilot()` is true, dim grey
otherwise) is mirrored in the header "STEREO / MONO" label.

## Input

### Rotary encoder (primary)

Unchanged from the OLED firmware:

- Rotate — tune frequency (in FREQ mode) or adjust volume (in VOL mode).
- Click — toggle between FREQ and VOL.
- Frequency mode wraps at the band edges (87.0 ↔ 108.0 MHz);
  volume clamps at 0 and `MAX_VOLUME` (63).

### Touch zones (alternative)

Tapping the screen forces focus to the tapped zone. Taps outside either zone
are ignored. Debounce is 200 ms; after a processed tap the loop sleeps 15 ms
to let the resistive XPT2046 settle — the same pattern as
`test_shield.cpp`.

| Tap target      | Rect (x, y, w, h)    | Action                 |
|-----------------|----------------------|------------------------|
| Frequency zone  | `(0, 32, 240, 108)`  | `MODE_FREQUENCY`       |
| Volume zone     | `(0, 256, 240, 48)`  | `MODE_VOLUME`          |

Touch calibration `{ 477, 3203, 487, 3356, 6 }` is hard-coded in
`main_tft.cpp`. If the shield is replaced, re-run the calibration phase
in `src/test_shield.cpp` and paste the resulting 5 numbers.

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
characters that fit on one line of FONT2. See
[future_improvements.md](future_improvements.md) for the marquee plan.

## Rendering pipeline

`updateDisplayTft()` in [src/main_tft.cpp](../src/main_tft.cpp) runs a
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

Sprites are not used in v1 — `fillRect` + `drawString` is fast enough and
far simpler. If future zones (waterfall, RT marquee) introduce visible
flicker, migrating the FREQ zone to a `TFT_eSprite` with DMA push is the
follow-up; the pattern is already proven in `test_shield.cpp` phase 5/6.

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
8. RSSI bar and SNR number track the live signal.
9. Version string (top-right of header, left of footer) matches
   `git describe --tags --dirty` and `strings firmware.elf | grep FW=`.

## Known limits

- **Battery meter:** no hardware in v1; the footer prints the literal
  `"USB"` via `POWER_SOURCE` in `ui_layout_tft.h`. Wiring a LiPo +
  voltage divider to an ADC pin + replacing the string with an
  `analogRead()` percentage is tracked in
  [future_improvements.md](future_improvements.md).
- **RDS RadioText scroll:** truncated to 38 chars; marquee scrolling is v2.
- **Single-FM-band:** `radio.cpp` wraps FM only. AM / SW / SSB can be added
  later without a UI rework.
- **No menu / presets / waterfall / brightness slider** — all v2; see the
  roadmap doc.
