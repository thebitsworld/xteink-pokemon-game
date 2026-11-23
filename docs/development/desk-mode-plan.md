---
title: Desk mode plan (X3 Lite)
parent: Development
nav_order: 30
---

# Desk mode plan (X3 Lite)

Status: **design, not started** (2026-11-20, branch `feat/x3-lite`). Screen mockups (portrait
528x792): <https://claude.ai/artifact/BnmoCtXQh1Anxk455VjPkX> (private to the owner).

Desk mode turns a spare Xteink X3 into a desk clock / dashboard: clock, Gregorian + Vietnamese
lunar calendar, weather, PC stats + Claude quota, photo frame and Pomodoro. It ships only in the
`pokemon-x3-lite` firmware (no Bluetooth, ~705 KB of flash free at 89.0%), behind
`CROSSINK_DESK=1`, so pokemon-x3 / X4 Pro / X4 Classic are unchanged.

## Decisions taken (with the owner, 2026-11-20)

| Topic | Decision |
|---|---|
| Firmware | `pokemon-x3-lite` only, compile flag `CROSSINK_DESK=1` |
| Where | A **Desk** entry on Home + setting **Start in Desk** (on: boot goes straight to Desk). Back leaves to Home; reading and Pokemon keep working |
| Orientation | Portrait 528x792, same as the reader; the 4 front buttons stay at the bottom |
| Layout | One main screen + sub-pages: PC + Claude, photo frame, Pomodoro; Settings from the menu |
| Power | Both USB and battery, with separate update intervals per source (table below) |
| Weather | Fetched **on the X3** from Open-Meteo (no API key), not through the PC |
| PC stats | The X3 polls a small agent on the PC over the LAN (plain HTTP, found by mDNS) |

## Screens

Mockups are the reference; text here only lists content and button roles.

1. **Main**: status bar (place, last update, battery or "charging"); big clock; weekday + date;
   lunar date `12/10 năm Bính Ngọ`, `Ngày Mậu Tuất · tháng Kỷ Hợi`; next lunar event ("Còn 3 ngày:
   Rằm tháng Mười"); weather card (icon, temp, condition, low/high, "rain around 15h", 4 hourly
   slots); a 3-bar strip CPU / RAM / Claude 5h (hidden when the agent is off).
2. **PC + Claude**: host name; bars for CPU, RAM, each disk; network down/up; uptime; Claude card
   with session (5 h) and weekly usage + reset times. Agent unreachable: "Không thấy máy tính" +
   last seen time.
3. **Photo frame**: one image from Photo Slideshow's folder with Photo Slideshow's settings
   (Fit/Crop, random order, interval; owner's choice 2026-11-23, instead of a fixed
   `/desk/photos`), drawn by the slideshow's own code (`PhotoRender`) fitted above a bottom
   strip with the date, lunar date, weather and index n/N (B/W pass only). The interval runs on
   USB and battery; on battery the page goes back to the clock after an hour without input.
4. **Pomodoro**: ring with minutes left, phase (Tập trung / Nghỉ), round n/4, end time, today's
   total. Running Pomodoro blocks deep power saving (see Power).
5. **Desk settings**: two-column list (USB / battery), rows below.
6. **Night sleep** (battery): static screen shown for the night window, wakes on any button.

Buttons (Back, Select, Up, Down = left pair, right pair):

| Screen | Back | Select | Up | Down |
|---|---|---|---|---|
| Main / PC / Photo | Exit to Home (PC, Photo: to Main) | Refresh now | Previous page | Next page |
| Pomodoro | To Main | Start / Pause | Skip phase | Reset |
| Settings | Back | Change value | Up | Down |

Long-press Select on Main opens Desk settings (no extra page in the cycle).

## Settings

Stored in `CrossPointSettings` (JSON on the SD card, existing mechanism), prefix `desk`.

| Setting | USB default | Battery default | Choices |
|---|---|---|---|
| Start in Desk | On | (shared) | On / Off |
| Clock update | 1 min | 1 min | 1 / 5 / 15 min / date only |
| Weather | 10 min | 60 min | 10 / 30 / 60 / 180 min |
| PC + Claude | 1 min | Off | 1 / 5 / 15 min / Off |
| Photo: next image | 30 min | Off | 5 / 30 / 60 min / Off |
| Full refresh (ghosting) | 1 h | 3 h | 1 / 3 / 6 h |
| Night sleep | Off | 23:00-06:00 | start / end hour, Off |
| Weather place | (shared) | | city name; empty = guess from IP |
| PC agent | (shared) | | Auto (mDNS) / fixed IP:port |
| Battery log | Off | On | On / Off |

City name and agent address are typed on the reader's web page (File Transfer), not with buttons.

## Architecture

```
src/activities/desk/
  DeskActivity.{h,cpp}        page state machine, input, scheduling, render dispatch
  DeskMainView / DeskPcView / DeskPhotoView / DeskPomodoroView / DeskSettingsView
  DeskScheduler.{h,cpp}       pure: next wake time per source from settings + power + clock
lib/Desk/                     pure C++, host-tested (test/desk/)
  Lunar.{h,cpp}               Ho Ngoc Duc algorithm in double, can-chi, VN festivals
  Weather.{h,cpp}             ported from esp32-pet pet/weather.{h,cpp}: WMO -> Sky, hot/cold,
                              hours until rain, report freshness
  OpenMeteoParse.{h,cpp}      JSON (JsonSax) -> WeatherReport
  AgentParse.{h,cpp}          agent JSON -> PcStats
src/network/DeskNet.{h,cpp}   one fetch round: Wi-Fi up, NTP (when due), weather, agent, Wi-Fi down
tools/desk-agent/             the PC agent (Python)
```

### Lunar calendar

- `double` arithmetic in C++ (the on-device Lua is 32-bit float, which is why this is not a Lua
  app). Time zone fixed at UTC+7.
- Output: lunar day/month/year, leap month flag, can-chi of day/month/year, next event from a fixed
  table (Tết, Rằm/mùng 1, Giỗ Tổ 10/3, Vu Lan 15/7, Trung thu 15/8, ông Công ông Táo 23/12) plus
  Gregorian holidays (1/1, 30/4, 1/5, 2/9).
- Tests: Tết dates 2000-2050 and the leap months of that range against a published table;
  20/11/2026 = 12/10 Bính Ngọ, day Mậu Tuất (checked while drawing the mockups).
- Computed once per day, cached.

### Weather (from esp32-pet, D36/D77)

- Request: `api.open-meteo.com/v1/forecast?latitude=..&longitude=..&current=temperature_2m,
  weather_code,is_day&hourly=weather_code,temperature_2m&forecast_hours=12&daily=
  temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto` (~1-2 KB).
- Place: `geocoding-api.open-meteo.com/v1/search?count=1&language=vi&name=<city>`, or
  `http://ip-api.com/json/?fields=status,city,lat,lon` when empty. Coordinates cached in settings.
- HTTPS through the existing wolfSSL streaming client (`SecureNet`), parsed with `JsonSax`, no
  certificate pinning needed for public data (as esp32-pet D36), one TLS session at a time.
- Rules ported as is: hot >= 33 C, cold < 17 C, stale after 3 h (hide the card's numbers, keep the
  last icon greyed), "rain around HH" from the hourly codes.
- Lessons from esp32-pet to keep: read bodies with our own loop that yields (no busy
  `readBytes`), declare the client before the HTTP object in the same scope.
- OpenWeatherMap (needs a key) is not in v1.

### PC agent (tools/desk-agent)

- Python 3, `psutil` + `zeroconf`, one file, runs at Windows log-on (Task Scheduler entry created
  by a documented command, not by the script).
- Advertises `_xdesk._tcp` on mDNS; serves `GET /v1/stats` on port 8765, LAN only, read-only, no
  auth (it exposes only the numbers below). The reader caches the resolved address and re-resolves
  on failure; a fixed IP:port setting skips mDNS.
- Response (< 1 KB):

```json
{"v": 1, "host": "DESKTOP-VU", "ts": 1795174800,
 "cpu": 23, "ram": [19.5, 32.0], "disks": [["C:", 412, 1000], ["D:", 1638, 2000]],
 "net": [2400000, 310000], "uptime": 274000,
 "claude": {"session": [42, 1795183200], "week": [18, 1795417200]}}
```

- `claude` is `null` until its source is settled (open question 1). Units: GB, bytes/s, seconds,
  percent; reset times are Unix seconds, formatted on the reader.

### Rendering

- Built-in UI fonts stop at Inter 12 and Lexend 16. The clock (~150 px), Pomodoro (~110 px) and
  temperature (~52 px) need **digit-only fonts** (`0-9 : ° % -`), generated with the existing
  `lib/EpdFont` tooling: estimate 10-25 KB total, measured in phase 1.
- Clock ticks redraw only the time box with a fast partial refresh; everything else redraws when
  its data changes; a full refresh on the "Full refresh" interval and when the page changes.
- Photos reuse the slideshow's decode + dither path (`SlideshowActivity`), not a new decoder.

## Power

Facts from the board config: the X3 has a DS3231 RTC and a BQ27220 fuel gauge (650 mAh cell) on
I2C, and **no USB-detect pin** (GPIO20 is I2C SDA). The SDK's RTC countdown wake
(`Rtc::armWakeTimer`) is RX8130-only, so it does not apply to the X3.

Plan, to be confirmed by phase 0 measurements on a real X3:

- **USB**: stay awake between ticks with the existing USB light-sleep loop (it already wakes every
  30 s); Wi-Fi rounds on the USB intervals.
- **Battery, minute ticks**: ESP32-C3 **light sleep with timer wake** (RAM, display state and Wi-Fi
  config kept; wake in ms), Wi-Fi off. Each tick: read clock, partial-refresh the time box, sleep.
  Deep sleep per minute is rejected: every deep-sleep wake is a full boot on this hardware.
- **Battery, long gaps** (night sleep, "date only"): deep sleep with ESP timer wake, boot straight
  into Desk (`Start in Desk`), no splash.
- **Charging detection**: from the fuel gauge (current sign / charging flag), since there is no pin.
- **Pomodoro running**: minute ticks as above, never deep sleep.

Rough budget (estimate, not measured): light-sleep floor + 1440 short wakes + 24 Wi-Fi rounds a day
comes to a few tens of mAh per day, i.e. two to five weeks on 650 mAh with the battery defaults.
Phase 0's battery log replaces this guess.

### Phase 0 results so far (Desk probe on the owner's X3, 2026-11-20)

| Probe | Result |
|---|---|
| v1/v2: plain light sleep, 59 s timer | `esp_light_sleep_start()` never returns (trace ends at `sleep-enter`) |
| v2: same tick loop with `delay()` | Ticks every minute; awake ~530 ms per tick, fast refresh 479 ms, half refresh 783 ms, about -21 mA awake |
| v3: light sleep with GPIO sleep switching off and display + SD pins held | A 10 s sleep wakes on the timer; the next 59 s sleep does not (no reboot either: a power-button press resumed the same boot) |
| v4: as v3, sleep cut into chunks of at most 10 s | **Ticks every minute** (owner, 5 min watched) |

So minute ticks on battery use light sleep with two rules, both required:

1. `esp_sleep_enable_gpio_switch(false)` and `gpio_hold_en()` on the display pins (SCLK, MOSI, CS,
   DC, RST) and the SD pins (CS, power enable) for the duration of the sleep.
2. No single light sleep longer than 10 s: re-arm the timer and sleep again (a chunk wake costs
   no render). Why longer sleeps never wake is not known; 10 s is the longest length tried.

Also seen: a sleep entered while the power button is held is rejected with
`ESP_ERR_INVALID_STATE` (retry after a short delay), and the BQ27220's RemainingCapacity reads
~2.9 Ah on a 650 mAh cell, so battery use is computed from SoC, voltage and current, not from it.
Unattended runs (2026-11-20 to 23) overturned this:

| Probe | Result |
|---|---|
| v4 overnight: light sleep in 10 s chunks, pins held | Ran 3 to 11 ticks per run, then one chunk never returned (trace ends at `sleep-enter`, the reader stayed hung until a power cycle). While it ran: gauge average about -3 mA, a tick awake ~590 ms |
| v5: deep sleep with an ESP timer wake per tick | No second tick, ever. The next boot (power button) found the RTC_NOINIT run marker gone, i.e. it was a power-on reset: on the X3 the chip appears to lose power in deep sleep (as the "Xteink wake is a power-on reset" note in platformio.ini says), so an ESP timer cannot wake it |

So on battery neither sleep mode gives unattended minute ticks yet. Options under discussion:
light sleep guarded by the RTC watchdog (a hung chunk resets the chip and Desk resumes), staying
awake at a low CPU clock (est. a few days per charge), or Desk on USB only with a static
"date only" screen on battery.

v6 (2026-11-23, chosen by the owner): light sleep in 10 s chunks, pins held, from a standalone
loop with button capture, Wi-Fi and the activity's render task out of the way, and the RTC
watchdog armed. 25 ticks without a hang or a reset, about -3 mA (est. ~9 days on 650 mAh);
the watchdog's recovery was not exercised. Likely reading: v4's hangs came from the reader's
background work around the sleep, not from light sleep itself.

## Phases

Each phase: its own commits (code and docs apart), `pio run -e pokemon-x3-lite` flash/RAM noted in
the commit message, host tests green, simulator check where it applies.

0. **Hardware probes on a real X3** (throwaway build): light sleep on battery keeps display + RTC
   and wakes on timer and on buttons; partial refresh time and quality of a 528x200 box; current
   draw via the gauge in light sleep / awake / Wi-Fi; charging detection from the gauge; battery
   log to `/.crosspoint/desk-battery.txt`. Result decides the power design above.
1. **Skeleton + main screen offline**: flag, Home entry, Start in Desk, DeskActivity with clock,
   date and lunar (+ tests), digit fonts, settings page (USB mode only). Simulator env
   `pokemon-simulator-X3-lite` with `CROSSINK_DESK=1`.
   *Done (2026-11-20):* flag, simulator env, digit fonts, DeskActivity main screen, lunar library
   and tests, opened from Applications > Desk. *Left:* Home entry, Start in Desk, settings page.
2. **Weather**: port rules + tests, Open-Meteo/geocoding/ip-api client, place setting on the web
   page, weather card + stale handling.
   *Done (2026-11-20):* rules, parsers and URL building in `lib/Desk` with tests; fetch through
   the last-used Wi-Fi network and `HttpDownloader`; card with drawn sky icons, low/high, rain
   hint, 4 slots (now, +3, +6, +9 h), stale handling. Place, until the settings page exists:
   `/.crosspoint/desk.json` `{"city": "Hà Nội"}` (empty or missing = locate by IP); the resolved
   coordinates are written back there, the last forecast to `/.crosspoint/desk-weather.json`.
   Fetched on entry when older than 10 min, every 10 min on USB (2 min after a failure), on
   Select. *Left:* city on the web page; not yet tried against the real API on the reader.
3. **PC agent**: `tools/desk-agent` + parser tests, mDNS discovery, PC page, main-screen strip,
   offline state.
   *Done (2026-11-20):* `tools/desk-agent/desk_agent.py` (psutil + zeroconf, `GET /v1/stats`,
   `_xdesk._tcp`), tried on the owner's Windows PC; `lib/Desk/AgentStats` parser and formatting
   with tests; on the reader, mDNS discovery (`MDNS.queryService`) or `"agent": "host:port"` in
   desk.json, PC page (CPU, RAM, up to 3 disks, network, uptime, Claude card), a 3-bar strip on
   the main screen while the numbers are under 5 min old, Up/Down page switching. Polled on
   entry, every minute on USB (Wi-Fi kept up on USB), on Select. Checked in the simulator against
   the agent running in WSL (the simulator has no mDNS queries, so it used the fixed address).
   *Left:* mDNS discovery on a real X3.
4. **Photo frame + Pomodoro** pages.
5. **Battery mode**: light-sleep ticks, Wi-Fi schedule, night sleep, full-refresh schedule,
   gauge-based source switching; 48 h battery log with defaults, then tune defaults.
   *Done (2026-11-23, first cut):* `src/activities/desk/DeskPower` (v6's sleep, pins, watchdog,
   RTC markers). On battery, 30 s after the last input (no Pomodoro, not on the photo page) the
   main screen enters a loop: minute ticks aligned to the RTC, weather every 60 min (Wi-Fi up and
   down), PC off, half refresh every 3 h and on a new day, "power button to use" instead of
   button hints. The power button or USB power leaves it by a restart straight back into Desk
   (`consumeOpenDeskRequest()` in setup); a watchdog reset resumes it from setup
   (`resumeBatteryFromBoot()`, gives up after 50 resets). *Left:* night sleep, the settings
   page's battery intervals, a long battery run on the reader.
   *Then (2026-11-23):* weather and PC fetches outside the battery loop run in a background task
   ("DeskNet") so the buttons answer at once; the two page buttons name the page they open.
6. **Claude quota** once open question 1 is settled.
   *Done (2026-11-23):* source = Claude Code's status line data (`rate_limits`), saved by
   `tools/desk-agent/desk_statusline.py` and served by the agent; the PC page stays up like the
   photo frame and its address can be typed on the reader (hold Select).

## Open questions

1. **Claude quota source.** Session/weekly percentages and reset times as shown by Claude Code's
   `/usage` are not known to be exposed in a stable, documented form. Options to check: data
   passed to a Claude Code status line command, local session logs (token counts only, no plan
   limit), an official API. Until settled the agent sends `"claude": null` and the reader hides
   the card.
2. ~~Does light sleep on battery behave on the X3?~~ Yes, with held pins and chunks of at most
   10 s (Phase 0 results). Still open: why one long light sleep never wakes.
3. Exact flash cost of digit fonts and the Desk code (budget: stay under ~92% so later features
   still fit).
