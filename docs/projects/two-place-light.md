[简体中文](/docs/projects/two-place-light.zh_CN.md) · **English**

# Two-Place Light

A small desk device that only cares about light: it shows how high the sun stands over
**Weihai** (home) and **Tianjin** (school) right now, when the sun comes up and goes down in
each place, and how much daylight is left today.

All of the astronomy is computed on the MCU. There are no image assets, no audio, and no fonts
beyond the two Montserrat sizes the baseline already enables. The network is used for one thing
only: setting the clock.

## Controls

| Control | Action |
| --- | --- |
| `UP` / `DOWN` | previous / next page |
| `OK` (short) | cycle backlight 100% → 60% → 30% |
| `OK` (hold) | screen off; any key wakes it again |
| `DOWN` (hold) | open the settings hotspot (places, UTC offset, Wi-Fi) |
| `OK` (hold) while acquiring time | give up on Wi-Fi, use manual set-time |
| `UP` / `DOWN` on the set-time page | adjust the highlighted field |
| `OK` (short) on the set-time page | next field, and *commit* on the last field |
| `OK` (hold) on the set-time page | cancel |
| any key while the settings hotspot is open | close it |

## Pages

1. **ARC** — the sun's elevation over the whole local day as two curves (blue = place 1,
   orange = place 2), a horizon line, and a dim vertical marker at the current time.
2. **PAIR** — numbers side by side: elevation now, sunrise, sunset, and which place is brighter.
3. **LEFT** — countdown to the next sunset or sunrise, civil dawn/dusk, and how much longer or
   shorter tomorrow's daylight is.

## Places

The two places are runtime data, not compile-time constants. They come from NVS and fall back to
Weihai (37.51 N, 122.12 E) and Tianjin (39.08 N, 117.20 E) when nothing has been stored. Change
them from the settings page:

- Names are ASCII only (letters, digits, space, `-`, `_`, up to 12 characters): the screen runs
  Montserrat, which has no CJK glyphs, and adding a font was deliberately skipped to save flash.
- Latitude −90..90, longitude −180..180.
- One UTC offset in minutes (default `480`, i.e. UTC+8) is shared by both places and must be a
  multiple of 15. Two places in different time zones are not supported: the local day boundary,
  the curve and "today" all use this single offset.

## Time

Three ways to obtain a credible clock, tried in this order when the RTC holds nothing usable:

1. **Wi-Fi + SNTP.** With stored credentials the device joins the network and asks
   `ntp.aliyun.com`; a successful sync writes the RTC. Both places are UTC+8, so no timezone
   table is needed.
2. **Settings hotspot.** Without credentials — or when joining fails — it opens a SoftAP
   `TwoPlaceLight` (WPA2, password `lingan-is-so-cute`). Join it from a phone, open
   `http://192.168.4.1`, and edit the two places, the UTC offset and (optionally) the Wi-Fi
   credentials; the device stores them in NVS and restarts. The same page is available later by
   holding `DOWN`. It is reachable by anyone in radio range while it is open, so it is a
   provisioning window, not a permanent service.
3. **Three buttons.** Holding `OK` during steps 1–2 skips the network entirely: `UP`/`DOWN`
   adjust the field, `OK` advances, `OK` on the minute field commits, holding `OK` cancels.

The board has no dedicated RTC chip. Time is kept by the ESP32-C3 RTC through newlib
(`CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC`), which survives deep sleep but is lost on a real power
cycle — hence the boot sequence above. A clock that is not credible shows `--:--`; nothing is
guessed.

Because the RTC slow clock runs from the internal RC oscillator (no 32.768 kHz crystal), a
retained clock drifts by minutes per day and survives a reset or reflash. The firmware therefore
re-syncs over SNTP whenever Wi-Fi credentials exist — once at boot even when the clock looks
credible, and then every six hours in a background task. A failed re-sync keeps the previous time
instead of blocking the UI, so the device never gets stuck on a network screen.

## Files

| Path | Role |
| --- | --- |
| `main/solar.c` | NOAA-derived sun geometry: declination, equation of time, rise/set, elevation, azimuth |
| `main/civil_time.c` | epoch ↔ civil date arithmetic, local day boundaries |
| `main/places.c` | runtime place/timezone state plus the pure validation rules |
| `main/places_store.c` | NVS persistence for places (per-field fallback to defaults) |
| `main/app_clock.c` | RTC read/write plus the last-sync record in NVS |
| `main/app_state.c` | page and backlight state machine |
| `main/app_setup.c` | three-button set-time state machine |
| `main/net_wifi.c` | STA join, settings SoftAP + HTTP page, NVS credentials |
| `main/net_time.c` | SNTP sync |
| `main/prov_parse.c` | settings form parsing (URL decoding, ranges, length rules) |
| `main/ui.c` | the five screens |

`main/demo_*.c` and the baseline pixel menu remain in the tree for reference but are no longer
compiled: derivative applications must ship their own UI.

## Not implemented yet

- Trustworthy time without network: with no Wi-Fi the clock is only as good as the RC-derived
  RTC, so drift is unbounded between manual set-time sessions.
- Real idle deep sleep (`bsp_display_prepare_deep_sleep` + `bsp_button_prepare_deep_sleep` +
  the key-wake guard in `main/key_guard.h`). Note that holding `OK` only blanks the screen: the
  CPU and Wi-Fi keep running, so it saves no power, it just hides the output. Because this
  application has no deep-sleep path at all, the upstream `tests/test_key_wake_contract.py`
  (a static contract that greps `main.c` for the wake-key guard wiring) was removed together
  with it; `main/key_guard.h` and `tests/test_key_guard.c` are kept for the eventual sleep work.
- Two places in different time zones (they share one UTC offset).
- Persisting the backlight step across reboots.
- Chinese labels; see the font checklist before adding any.

## Changing the places

Hold `DOWN` on the device, join `TwoPlaceLight` from a phone and open `http://192.168.4.1`; the
device restarts after you save. The factory default (Weihai and Tianjin) lives in
`main/places.c` as `PLACES_DEFAULT`, and uses city-centre approximations (±0.05°), far below one
minute of sunrise accuracy.
