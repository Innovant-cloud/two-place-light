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
| `OK` (hold) while acquiring time | give up on Wi-Fi, use manual set-time |
| `UP` / `DOWN` on the set-time page | adjust the highlighted field |
| `OK` (short) on the set-time page | next field, and *commit* on the last field |
| `OK` (hold) on the set-time page | cancel |

## Pages

1. **ARC** — the sun's elevation over the whole local day as a curve, a horizon line, and two
   markers at the current time: `*` solid = Weihai, `o` ring = Tianjin.
2. **PAIR** — numbers side by side: elevation now, sunrise, sunset, and which place is brighter.
3. **LEFT** — countdown to the next sunset or sunrise, civil dawn/dusk, and how much longer or
   shorter tomorrow's daylight is.

## Time

Three ways to obtain a credible clock, tried in this order when the RTC holds nothing usable:

1. **Wi-Fi + SNTP.** With stored credentials the device joins the network and asks
   `ntp.aliyun.com`; a successful sync writes the RTC. Both places are UTC+8, so no timezone
   table is needed.
2. **Provisioning hotspot.** Without credentials — or when joining fails — it opens a SoftAP
   `TwoPlaceLight` (WPA2, password `twoplacelight`). Join it from a phone, open
   `http://192.168.4.1`, enter the Wi-Fi name and password; the device stores them in NVS and
   restarts. The setup page is reachable by anyone in radio range while it is open, so it is a
   provisioning window, not a permanent service.
3. **Three buttons.** Holding `OK` during steps 1–2 skips the network entirely: `UP`/`DOWN`
   adjust the field, `OK` advances, `OK` on the minute field commits, holding `OK` cancels.

The board has no dedicated RTC chip. Time is kept by the ESP32-C3 RTC through newlib
(`CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC`), which survives deep sleep but is lost on a real power
cycle — hence the boot sequence above. A clock that is not credible shows `--:--`; nothing is
guessed.

## Files

| Path | Role |
| --- | --- |
| `main/solar.c` | NOAA-derived sun geometry: declination, equation of time, rise/set, elevation, azimuth |
| `main/civil_time.c` | epoch ↔ civil date arithmetic, local day boundaries |
| `main/places.c` | the two coordinates and their display names |
| `main/app_clock.c` | RTC read/write plus the last-sync record in NVS |
| `main/app_state.c` | page and backlight state machine |
| `main/app_setup.c` | three-button set-time state machine |
| `main/net_wifi.c` | STA join, provisioning SoftAP + HTTP form, NVS credentials |
| `main/net_time.c` | SNTP sync |
| `main/prov_parse.c` | provisioning form parsing (URL decoding, length rules) |
| `main/ui.c` | the five screens |

`main/demo_*.c` and the baseline pixel menu remain in the tree for reference but are no longer
compiled: derivative applications must ship their own UI.

## Not implemented yet

- Background re-sync: the device only syncs at boot, and only when the clock is not credible.
- Idle deep sleep (`bsp_display_prepare_deep_sleep` + `bsp_button_prepare_deep_sleep` + the
  key-wake guard in `main/key_guard.h`). Without it the device draws full runtime current
  continuously.
- Persisting the backlight step across reboots.
- Chinese labels; see the font checklist before adding any.

## Changing the places

Edit `main/places.c`. Coordinates are city-centre approximations (±0.05°), which is far below one
minute of sunrise accuracy.
