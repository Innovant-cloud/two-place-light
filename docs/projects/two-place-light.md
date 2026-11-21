[简体中文](/docs/projects/two-place-light.zh_CN.md) · **English**

# Two-Place Light

A small desk device that only cares about light: it shows how high the sun stands over
**Weihai** (home) and **Tianjin** (school) right now, when the sun comes up and goes down in
each place, and how much daylight is left today.

Everything is computed on the MCU. No network, no image assets, no audio, no fonts beyond the
two Montserrat sizes the baseline already enables.

## Controls

| Control | Action |
| --- | --- |
| `UP` / `DOWN` | previous / next page |
| `OK` (short) | cycle backlight 100% → 60% → 30% |
| `OK` (hold) | screen off; any key wakes it again |
| `UP` / `DOWN` in the set-time page | adjust the highlighted field |
| `OK` (short) in the set-time page | next field, and *commit* on the last field |
| `OK` (hold) in the set-time page | cancel |

## Pages

1. **ARC** — the sun's elevation over the whole local day as a curve, a horizon line, and two
   markers at the current time: solid = Weihai, ring = Tianjin.
2. **PAIR** — numbers side by side: elevation now, sunrise, sunset, and which place is brighter.
3. **LEFT** — countdown to the next sunset or sunrise, civil dawn/dusk, and how much longer or
   shorter tomorrow's daylight is.

## Time

The board has no dedicated RTC chip. Time comes from the ESP32-C3 RTC through newlib
(`CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC`), which survives deep sleep — but that only matters once
idle sleep exists, and **it does not yet**. Until then the device refreshes once a second while
awake and the RTC is lost on a real power cycle.

Because no input channel other than the three buttons has ever been verified in this repository,
setting the clock is a three-button flow: the device boots into the set-time page whenever the RTC
holds no credible time, and pre-fills the last sync timestamp stored in NVS as a starting point.
Nothing pretends the time is known when it is not.

## Not implemented yet

- Idle deep sleep (`bsp_display_prepare_deep_sleep` + `bsp_button_prepare_deep_sleep` + the
  key-wake guard from `main/key_guard.h`). Without it the device draws its full runtime current
  continuously, which a 500 mAh cell will not enjoy.
- Persisting the backlight step across reboots.
- Chinese labels; see the font checklist before adding any.


## Files

| Path | Role |
| --- | --- |
| `main/solar.c` | NOAA-derived sun geometry: declination, equation of time, rise/set, elevation, azimuth |
| `main/civil_time.c` | epoch ↔ civil date arithmetic, local day boundaries |
| `main/places.c` | the two coordinates and their display names |
| `main/app_clock.c` | RTC read/write plus the last-sync record in NVS |
| `main/app_state.c` | page and backlight state machine |
| `main/app_setup.c` | three-button set-time state machine |
| `main/ui.c` | the four screens |

`main/demo_*.c` and the baseline pixel menu remain in the tree for reference but are no longer
compiled: derivative applications must ship their own UI.

## Changing the places

Edit `main/places.c`. Coordinates are city-centre approximations (±0.05°), which is far below
one minute of sunrise accuracy.
