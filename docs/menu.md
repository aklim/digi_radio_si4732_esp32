# TFT menu & multi-band — short spec

This document captures the long-press menu + multi-band UX shipped as
step 2 of the [ATS-Mini](https://github.com/esp32-si4732/ats-mini) parity
roadmap. It covers only the current behaviour — future additions
(step selector, memory presets, seek, SSB / BFO, settings, themes, …)
will extend this file rather than replace it.

Single product env — the 128×64 SSD1315 OLED build has been retired.
Everything below describes the `esp32dev` firmware running on the
Waveshare 2.8" TFT shield.

## Bands

| Idx | Name          | Mode | Range            | Step   |
|-----|---------------|------|------------------|--------|
| 0   | FM Broadcast  | FM   | 87.0–108.0 MHz   | 100 kHz |
| 1   | MW            | AM   | 520–1710 kHz     | 10 kHz  |
| 2   | SW 41m        | AM   | 7100–7300 kHz    | 5 kHz   |
| 3   | SW 31m        | AM   | 9400–9900 kHz    | 5 kHz   |

Adding a band is a one-line change — append a `Band` entry to `g_bands[]`
in [src/radio.cpp](../src/radio.cpp) and the menu picks it up
automatically. The Si4735 is reconfigured via `setFM()` / `setAM()` with
the new range on every switch. SSB (LSB / USB) is reserved in the
`BandMode` enum but not yet wired to the chip's SSB patch loader.

RDS and the stereo pilot indicator are FM-only: poll is a no-op on AM
bands, and the header omits the "STEREO / MONO" label on non-FM to avoid
displaying a feature the chip isn't decoding.

## Menu structure

Opened by **holding the encoder button for ≥500 ms**. Click-then-release
before 500 ms still toggles FREQ ↔ VOL mode as before. The
long-press threshold lives in
[src/input.cpp](../src/input.cpp) (`BTN_LONG_PRESS_MS`).

```
┌─ Menu ────────────────┐
│  Band                 │   <- highlighted
│  Close                │
│                       │
│ Rotate = select       │
│ Click  = confirm      │
└───────────────────────┘
                │ click "Band"
                ▼
┌─ Band ────────────────┐
│  FM Broadcast     *   │   <- "*" = currently active band
│  MW                   │
│  SW 41m               │
│  SW 31m               │
│  < Back               │
│                       │
│ * = current band      │
└───────────────────────┘
```

Controls inside the menu:

- Rotate encoder — move the highlight. Wraps at both ends.
- Click — confirm: descend into a sub-list or commit a selection.
- Long-press — back out to the main UI without changing anything (a
  quick escape hatch if you opened the menu by accident).

Selecting a band row switches immediately, saves the change to NVS, and
closes the menu. `< Back` in the band picker returns to the top list
without touching the radio.

## Persistence

Runtime state is stored in the ESP32 NVS partition via `<Preferences.h>`
(namespace `"radio"`). Keys:

- `ver`       — `PERSIST_SCHEMA_VER`. Stale schemas trigger a namespace
                wipe on boot.
- `band`      — current band index (0..3 at the moment).
- `vol`       — global volume 0..63.
- `freq<N>`   — per-band last-tuned frequency, where `<N>` is the band
                index. Stored in the band's native unit (10 kHz for FM,
                1 kHz for AM).

Writes are coalesced with a 1-second rate limit inside
[src/persist.cpp](../src/persist.cpp), so rapid encoder rotation doesn't
hammer the flash. `persistFlush()` forces any pending writes (intended
for a future sleep path).

## Files involved

- [include/menu.h](../include/menu.h) /
  [src/menu.cpp](../src/menu.cpp) — menu state machine + rendering.
- [include/persist.h](../include/persist.h) /
  [src/persist.cpp](../src/persist.cpp) — NVS wrapper.
- [include/input.h](../include/input.h) /
  [src/input.cpp](../src/input.cpp) — `ButtonEvent` enum + long-press
  detection + `encoderSetBoundsForMenu()`.
- [include/radio.h](../include/radio.h) /
  [src/radio.cpp](../src/radio.cpp) — `Band` struct, band table,
  `radioSetBand()`, `radioFormatFrequency()`.
- [src/main.cpp](../src/main.cpp) — routing between main UI and
  menu, persistence calls on user actions, mode-aware header / footer.

## Verification checklist

After flashing `pio run -e esp32dev -t upload`:

1. Boot: splash, then main UI on the last-saved band + freq + volume
   (FM Broadcast @ 102.40 MHz, volume 30 on first boot).
2. Short-click: FREQ ↔ VOL toggle still works (regression).
3. Long-press (≥500 ms): menu opens with `Band` highlighted.
4. Rotate: cursor moves, wraps at both ends.
5. Click `Band`: sub-list shows all four bands with `*` marking the
   current one.
6. Click `SW 41m`: header reads "SW 41m", frequency displays "7200 kHz",
   menu closes, encoder rotation tunes in 5 kHz steps.
7. RDS zone clears (no RDS on AM); stereo label disappears.
8. Tune a few detents, reboot — radio comes back up on SW 41m at the
   new frequency (NVS roundtrip).
9. Long-press → rotate → long-press (inside menu): menu closes, main UI
   repaints.
10. Rapid encoder rotation: watch Serial — `persistSaveFrequency` writes
    should settle at ≥1 s intervals, not per-detent.
