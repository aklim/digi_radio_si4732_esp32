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
│  BW                   │
│  AGC                  │
│  Theme                │
│  Scan                 │
│  Settings             │
│  Close                │
│ Rotate = select       │
│ Click  = confirm      │
└───────────────────────┘
     │ click one of:
     ├─ "Band"     → band picker
     ├─ "BW"       → IF-filter picker (mode-dependent list)
     ├─ "AGC"      → AGC / manual attenuator picker
     ├─ "Theme"    → palette picker
     ├─ "Scan"     → start bandscope sweep, close menu
     ├─ "Settings" → feature toggles (RDS / Bluetooth / WiFi)
     └─ "Close"    → close menu

┌─ Band ────────────────┐
│  FM Broadcast     *   │   <- "*" = currently active band
│  MW                   │
│  SW 41m               │
│  SW 31m               │
│  < Back               │
│ * = current band      │
└───────────────────────┘

┌─ BW (FM) ─────────────┐    ┌─ BW (AM/SW) ──────────┐
│  Auto             *   │    │  1.0k                 │
│  110k                 │    │  1.8k                 │
│  84k                  │    │  2.0k                 │
│  60k                  │    │  2.5k                 │
│  40k                  │    │  3.0k             *   │
│  < Back               │    │  4.0k                 │
│ * = active filter     │    │  6.0k                 │
└───────────────────────┘    │  < Back               │
                             │ * = active filter     │
                             └───────────────────────┘

┌─ AGC ─────────────────┐
│  AGC On           *   │   <- row 0: AGC enabled
│  AGC Off              │   <- row 1: AGC off, no attenuation
│  Att 01               │
│  Att 02               │
│  …                    │   <- FM: up to Att 27 / AM: up to Att 37
│  < Back               │
│ * = active AGC        │
└───────────────────────┘

┌─ Settings ────────────┐
│  RDS: On              │   <- click toggles On ↔ Off
│  Bluetooth: Off       │
│  WiFi: Off            │
│  < Back               │
│ Click = toggle        │
└───────────────────────┘
```

Controls inside the menu:

- Rotate encoder — move the highlight. Wraps at both ends; long lists
  (AGC) scroll so the cursor stays on-screen.
- Click — confirm: descend into a sub-list or commit a selection.
- Long-press — back out to the main UI without changing anything (a
  quick escape hatch if you opened the menu by accident).

### Pickers

- **Band** — switches immediately, saves the new band to NVS, and closes
  the menu. `< Back` returns to the top list without touching the radio.
- **BW** — list adapts to the active band's mode: 5 rows on FM (filters
  `Auto`, `110k`, `84k`, `60k`, `40k`), 7 rows on AM/SW (`1.0k` .. `6.0k`
  step tables). FM and AM keep independent selections so switching
  modes restores each mode's preferred filter.
- **AGC** — row 0 enables AGC, row 1 disables it with no attenuation,
  rows 2+ are manual attenuator steps (`Att 01` .. `Att NN`). FM
  supports up to `Att 27`, AM/SW up to `Att 37`. Indices follow ATS-Mini
  `doAgc` semantics: row `N` writes AGCIDX = `N-1` with AGCDIS=1.
- **Theme** — one row per palette in [src/Themes.cpp](../src/Themes.cpp);
  selection applies immediately and persists.
- **Scan** — kicks off the bandscope sweep centred on the current tune
  and closes the menu. During the sweep the encoder is ignored; once
  the sweep completes, the encoder tunes normally and the graph scrolls
  to track the new frequency (matches ATS-Mini CMD_SCAN). Click exits
  the scan keeping the current tune; long-press exits and restores the
  pre-scan frequency.
- **Settings** — feature toggles. Each click flips RDS / Bluetooth /
  WiFi between On and Off and saves the new state to NVS; the submenu
  stays open so multiple toggles can be flipped without re-entering.
  Disabling RDS short-circuits the Si4735's RDS decoder and the Core-0
  poll loop (no I²C traffic, PS/RT/PI mirrors cleared). Bluetooth and
  WiFi flags only gate the header indicator icons today — the real
  radio stacks land in follow-up PRs.

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
- `theme`     — active UI palette index (see
                [src/Themes.cpp](../src/Themes.cpp)). Added in v2.
- `bw_fm`     — FM IF-filter index (0..4). Added in v3. Default 0 (Auto).
- `bw_am`     — AM/SW IF-filter index (0..6). Added in v3. Default 4 (3.0k).
- `agc_fm`    — FM AGC/attenuator index (0..27). Added in v3. Default 0 (AGC on).
- `agc_am`    — AM/SW AGC/attenuator index (0..37). Added in v3. Default 0.
- `rds_en`    — RDS decode enable (0/1). Added in v4. Default 1 (on).
- `bt_en`     — Bluetooth enable (0/1). Added in v4. Default 0 (off).
- `wifi_en`   — WiFi enable (0/1). Added in v4. Default 0 (off).

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
11. Long-press → **BW** → pick a non-default filter (e.g. `60k` on FM).
    Sidebar BW row updates; reboot → filter persists.
12. Switch to MW via `Band` picker. Open `BW` again — list now shows 7
    AM-catalogue entries; default is `3.0k`. Pick `1.8k`, switch back
    to FM — the FM filter (`60k`) is restored, proving per-mode shadows.
13. Long-press → **AGC** → pick `Att 05`. On a strong FM station the
    audio audibly attenuates; sidebar row flips from `AGC:On` to
    `Att:5`. Reboot → attenuator persists.
14. Long-press → **Scan**. Wait for `[scan] complete` on serial (~15 s).
    Rotate encoder → tune shifts and the graph scrolls to follow.
    Click → scan exits at the new tune. Re-enter scan → long-press
    → scan exits and restores the pre-scan frequency.
