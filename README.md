# epaper-calendar

ESP8266 firmware that shows today's schedule and todos from [calendar](https://github.com/Alexander-D-Karpov/calendar) on a 4.2" black/white/red e-paper panel.

![Screens: morning, day, evening (offline), night, sleep](docs/screens.png)

## Features

- Current / next event, day timeline, all-day events, overdue todos (up to 14 days)
- Outdoor temperature and weather icon (clear, cloudy, fog, drizzle, rain, snow, thunder); the sky picture follows the weather too
- Four themes driven by the real sun position (St. Petersburg by default): morning, day, evening, night
- Sleep screen during the sleep window from the account settings
- Live clock, countdowns and progress via fast b/w partial refresh, no flashing
- Full color refresh only when something structural changes, plus a periodic clean-up
- Offline cache in LittleFS, so the screen keeps working without Wi-Fi
- FLASH button toggles a custom picture

## Hardware

| Part | Notes |
|---|---|
| NodeMCU v1.0 (ESP8266, ESP-12E) | 4 MB flash, USB power |
| WeAct Studio 4.2" e-paper, 400×300, b/w/red | GDEY042Z98, SSD1683 controller |
| 8 jumper wires | |

## Wiring

| Display | NodeMCU | GPIO |
|---|---|---|
| VCC | 3V3 | — |
| GND | GND | — |
| SDA (MOSI) | D7 | 13 |
| SCL (SCK) | D5 | 14 |
| CS | D6 | 12 |
| DC | D2 | 4 |
| RES | D1 | 5 |
| BUSY | D0 | 16 |

Power the panel from 3V3, not VIN. The FLASH button (GPIO0) needs no wiring. Don't hold it during reset, or the board enters bootloader mode.

## Setup

1. Create an API token in the calendar under Settings → API tokens, with scope `display:read`. The calendar must be recent enough to serve `/api/v1/display`.
2. Fill in `.env`:

   ```bash
   cp .env.example .env
   ```

   | Key | Required | Default |
   |---|---|---|
   | `WIFI_SSID`, `WIFI_PASS` | yes | — |
   | `CAL_TOKEN` | yes | — |
   | `CAL_HOST` | no | `calendar.akarpov.ru` |
   | `TZ_POSIX` | no | `MSK-3` |
   | `GEO_LAT`, `GEO_LON` | no | St. Petersburg |
   | `DISPLAY_ROTATION` | no | `1` (portrait) |
   | `CAL_INSECURE` | no | `0` (set `1` to skip TLS verification) |

3. Build and flash with [PlatformIO](https://platformio.org):

   ```bash
   pio run -t upload
   pio device monitor
   ```

   `.env` is read at build time and baked into the firmware. On the first build, PT Sans is downloaded and rasterized into 1-bit fonts with Cyrillic (`scripts/fonts.py`, needs `freetype-py`, installed automatically).

## How it works

- Polls `/api/v1/display` every 3 minutes with `If-None-Match`, so an unchanged day costs a bodyless `304`. One reply carries today and tomorrow: events, todos, sleep windows and weather.
- Weather comes from [Open-Meteo](https://open-meteo.com) through the calendar server for `GEO_LAT`/`GEO_LON`, cached there for 15 minutes. It is hidden if older than 3 hours.
- Partial refresh runs every minute for the clock, countdowns, progress bar, "now" marker and weather line. These areas are kept strictly black/white because the panel's fast mode can't drive red.
- A full refresh happens on event start/end, sun phase or weather type change, new data, a new day, sleep, or an online/offline switch. A clean-up full refresh runs every 4 hours, and one is forced if the screen has not changed for 10 minutes.
- TLS uses the Let's Encrypt roots (ISRG X1/X2). The body is read whole, TLS is closed, then JSON is parsed. A second IRAM heap holds the TLS buffers.

## Layout

```
scripts/   env.py (.env → build flags), fonts.py (font generator)
src/       app      scheduler, refresh logic, button
           api      HTTPS client for /api/v1/display
           model    snapshot layout and string pool
           store    LittleFS snapshot cache
           render   screens, dithering, live regions
           agenda   current/next/tomorrow/todos selection
           sun      NOAA sun position and day phases
           text     UTF-8 → font code page, wrapping, ellipsis
           picture  bitmap shown by the FLASH button
```

Tunables (refresh intervals, limits) live in `src/config.h` and `src/model.h`.

## License

[MIT](LICENSE)
