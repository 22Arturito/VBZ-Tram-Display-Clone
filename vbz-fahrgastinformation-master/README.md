See https://sschueller.github.io/posts/vbz-fahrgastinformation/ for original project details.

# VBZ Fahrgastinformation Display

![Finished Sign](https://sschueller.github.io/posts/vbz-fahrgastinformation/P_20221106_172806.jpg)

ESP32-S3 powered real-time tram departure display using 3× chained 64×64 HUB75 LED matrix panels (192×64px total). Departure data from [opentransportdata.swiss](https://opentransportdata.swiss) OJP 2.0 API.

---

> **Original project by [sschueller](https://github.com/sschueller)**
> The sections below marked **(original)** are from the original README and describe the base project.
> Everything else documents modifications and additions made on top of that work.

---

## Features (original)

- Gets live data from opentransportdata.swiss API
- Configurable station
- Shows scheduled data if live data is not available. Live data is marked with a fat tic vs regular tic
- Shows a `>` if the actual time is over 3 minutes from scheduled time
- Shows train icon when the time is under 1 minute
- Shows if train or bus has level entry (Accessibility)
- Shows VBZ tram colors for known lines
- Limited direction filter (Outbound vs Return)
- Auto display brightness

## Features (added)

- Two configurable stations, switchable via button or web interface
- Direction filter: both / outbound (H) / inbound (R), persisted to flash
- Night mode: amber colors, low brightness, auto-scheduled (22:00–06:00) with manual override
- Clock screensaver with date and live weather (Open-Meteo, no API key needed)
- Web configuration UI with live departure view and station search
- OTA firmware updates over WiFi

## Todo (original)

- Show service issues at a particular station however this data may not be available via opentransportdata.swiss
- Better station direction filter to only show trips at a particular platform
- 3d printed case for desk or wall mount
- ~~OTA updates~~ ✓ done
- Custom PCB for better PCB to Display connection

## Required Hardware (original)

You will need an ESP and the matrix display. For exact BOM see: https://sschueller.github.io/posts/vbz-fahrgastinformation/

## Software Setup (original)

1. Open this project in PlatformIO
2. Copy `include/Config.h.dist` to `include/Config.h`
3. Edit `include/Config.h` to match your API key, station etc.

For a list of stations download the xlsx file located here: https://opentransportdata.swiss/de/dataset/bav_liste

---

## Features

- Live departure data from opentransportdata.swiss (falls back to scheduled if live unavailable)
- Two configurable stations, switchable via button or web interface
- Direction filter: both / outbound (H) / inbound (R)
- VBZ tram line colors for all known Zürich lines
- Accessibility indicator (low-floor vehicles)
- Live/scheduled marker (`'` = scheduled, `` ` `` = live), `>` prefix if running late
- Night mode: amber colors, low brightness, auto-scheduled (22:00–06:00) with manual override
- Clock screensaver with date and live weather (Open-Meteo, no API key needed)
- Web configuration UI with live departure view and station search
- OTA firmware updates over WiFi

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | Freenove ESP32-S3 WROOM (8MB Flash / 8MB PSRAM) |
| Display | 3× 64×64px HUB75 LED matrix panels, chained (192×64px total) |
| Button | Single push button |

For full BOM and wiring see: https://sschueller.github.io/posts/vbz-fahrgastinformation/

---

## First-time setup

### 1. Configure `include/Config.h`

```cpp
#define OPEN_DATA_API_KEY "your_key_here"  // from opentransportdata.swiss
#define WEATHER_LAT "47.37"                // your latitude
#define WEATHER_LON "8.54"                 // your longitude
#define BRIGHTNESS_FIXED 80                // 0–255, or -1 for ambient light sensor
```

Get a free API key at [opentransportdata.swiss](https://opentransportdata.swiss).
Find station BPUIC IDs in the xlsx at https://opentransportdata.swiss/de/dataset/bav_liste

### 2. Flash via USB

Connect the board with a **data** USB cable (not charge-only):

```bash
pio run -e freenove_esp32_s3_wroom -t upload
```

### 3. Connect to WiFi

On first boot the display shows **"connect to: vbz-anzeige"**. Connect to that WiFi network (password: `123456`), open a browser — the portal opens automatically. Enter your home WiFi credentials and save.

### 4. Configure stations

After connecting, the display shows its IP address. Open `http://<IP>/config` to set your stations, night hours, and brightness.

---

## OTA firmware updates

After the first USB flash, all future updates are wireless.

Set the device IP in `platformio.ini`:
```ini
upload_port = 192.168.1.22   ; your display's IP
```

Then upload wirelessly:
```bash
pio run -e ota -t upload
```

OTA password: `vbz1234` (configurable in `main.cpp` → `ArduinoOTA.setPassword`).

To pass the IP directly without editing the file:
```bash
pio run -e ota -t upload --upload-port 192.168.1.xx
```

The display shows "connecting..." during upload and reboots automatically when done.

---

## Web interface

Open `http://<IP>/config` in any browser on the same network.

### Controls

| Button | Action |
|---|---|
| Switch Station | Toggle between Station 1 and Station 2 |
| Switch to Night / Day | Toggle night mode manually |
| Dir: Both / H (out) / R (in) | Cycle direction filter |
| Turn Off / On | Toggle display on/off |
| Clock: Off / On | Toggle clock screensaver |
| Test Display | Flash red/green/blue/white to verify panels |

### Live Departures

Shows current departures from the active station, auto-refreshes every 30 seconds. Direction is color-coded: green = outbound, blue = inbound.

### Station Search

Click **Search** next to a Name field to look up a stop by name. Click a result to fill the BPUIC ID automatically.

### Settings (persisted to flash)

| Setting | Description |
|---|---|
| Station 1 / 2 BPUIC ID | Numeric stop identifier |
| Station 1 / 2 Name | Display label |
| Night start / end hour | Auto night mode schedule (e.g. 22 / 6) |
| Brightness | 0–255 fixed, or -1 for ambient sensor |

---

## Physical button

| Press | Action |
|---|---|
| Single press | Switch between Station 1 and Station 2 |
| Double press | Toggle night mode (manual override) |
| Long press (>800ms) | Toggle display off / on |

Manual night mode override is cleared automatically at the next scheduled boundary.

---

## Clock screensaver

Shows time (HH:MM:SS), date (DD.MM.YYYY), and current weather. Weather is fetched from [Open-Meteo](https://open-meteo.com) — no API key required, refreshes every 10 minutes. Location is set via `WEATHER_LAT` / `WEATHER_LON` in `Config.h`.

---

## Night mode

Activates automatically between configured hours (default 22:00–06:00):
- All colors switch to amber
- Brightness drops to ~10%

Manual override via button double-press or web interface. Override clears at the next scheduled boundary.

---

## Project structure

```
include/
  Config.h                  — pin definitions, API key, constants
src/
  main.cpp                  — application logic, web handlers, main loop
lib/
  Display/                  — HUB75 rendering, font, layout logic
  OpenTransportDataSwiss/   — OJP 2.0 API client
platformio.ini              — build environments (USB + OTA)
```

---

## Dependencies

- [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA)
- [ArduinoJson](https://arduinojson.org) 6.x
- [AutoConnect](https://hieromon.github.io/AutoConnect/)
- [NTPClient](https://github.com/taranais/NTPClient)
- [Open-Meteo](https://open-meteo.com) — weather, no key required
- [opentransportdata.swiss](https://opentransportdata.swiss) OJP 2.0 — free API key required
