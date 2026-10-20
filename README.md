# HueAlarm

A battery-powered bedside alarm clock (inspired by the Braun BC08) for the **M5Stack Core2 v1.1** that triggers a Philips Hue sunrise effect ahead of the alarm time.

Between uses, the device either enters ESP32 deep sleep (waking on screen touch) or powers off completely (waking on the side button or RTC timer), achieving ~1 month to several months of battery life while keeping Wi-Fi completely off except when starting the sunrise or syncing the clock once a week.

## Features

- **Braun BC08-style clock face** with high-contrast segment digits, bell indicator, sun indicator, snooze state, and low-battery / charging indicator.
- **Philips Hue CLIP v2 integration:**
  - Discovers the Hue Bridge automatically via mDNS (`_hue._tcp`) or `discovery.meethue.com`.
  - Pairs directly from the touchscreen (press the link button on the bridge).
  - Select any Hue room or zone (or match a lamp by name/product, e.g. `Hue Twilight`) and test the sunrise effect from the settings screen.
  - Starts the native Hue `sunrise` timed effect before the alarm time.
- **On-device Wi-Fi setup via WPS** (with optional compile-time fallback credentials in `include/secrets.h`).
- **Smooth momentum scroll wheels** for setting the alarm time (5-minute increments).
- **Gradual alarm tone:** Pre-rendered 4-beep cycle with volume ramping up over 60 seconds; tap anywhere to snooze (9 minutes) or tap the bell to stop.
- **Fast wake-up & ultra-low sleep power:** ~345 ms from touch wake to drawn clock face by skipping the startup PSRAM test, keeping unused peripherals off, and putting the AXP2101 rails, INA3221 current monitor, and FT6336U touch controller into low-power modes. See [`docs/POWER.md`](docs/POWER.md) for detailed measurements.

## Hardware

- **M5Stack Core2 v1.1** (ESP32, 2.0" 320×240 IPS display, FT6336U touch controller, BM8563 RTC, AXP2101 PMIC, INA3221 current monitor).
  - *Core2 v1.1 vs. v1.0:* The sleep-current optimizations in [`src/app/power.cpp`](src/app/power.cpp) shut down unused rails on the **AXP2101** PMIC and power down the **INA3221** current monitor specific to the v1.1 revision (on an older v1.0 with an AXP192, `M5Unified` handles basic sleep and skips the v1.1-specific register writes).
  - *Core2 vs. CoreS3:* An `env:cores3` target for the **M5Stack CoreS3** is included in [`platformio.ini`](platformio.ini) and builds cleanly, but in practice the CoreS3's internal Wi-Fi antenna was too weak at bedside distance; the Core2 v1.1 connects reliably in ~2 seconds.
- **M5Stack Battery Module 13.2 (1500 mAh)** stacked with the standard bottom battery (~1600 mAh total):
  - Gives ~1 month per charge in touch-wake deep-sleep mode (`HUEALARM_WAKE_TOUCH`, ~2 mA) or several months in button-wake power-off mode (`HUEALARM_WAKE_BUTTON`, ~0 mA, switched on for alarms by the BM8563 RTC timer). See [`docs/POWER.md`](docs/POWER.md) for discharge curves and hardware quirks.
- **Philips Hue Bridge** (v2, local HTTPS CLIP v2 API) and at least one sunrise-capable Hue light:
  - Designed around the **Philips Hue Twilight** bedside lamp (default match `HUEALARM_LAMP_MATCH = "Twilight"`), though any Hue room or zone with `sunrise`-capable lights can be selected directly on the touchscreen.

## Controls

### Clock Screen
- **Tap the bell (bottom-left):** Toggle the alarm on / off.
- **Long-press the bell:** Toggle between **Light + Beep** (sun icon visible) and **Beep only**.
- **Tap the alarm time (bottom-right):** Open the scroll-wheel picker to set the alarm time.
- **Long-press anywhere else:** Open the **Settings** screen.

### While Ringing
- **Tap anywhere:** Snooze for 9 minutes.
- **Tap the bell:** Stop the alarm for today.

### Settings Screen
- **Sunrise before alarm:** Cycle sunrise lead time (`off`, `5`, `10`, `15`, `20`, `30` min).
- **Wake-up light:** Discover/pair with the Hue Bridge, pick a room or zone, and run a 1-minute test sunrise.
- **Sync time now:** Connect to Wi-Fi and sync the hardware RTC via NTP.
- **Connect Wi-Fi (WPS):** Join a 2.4 GHz network using your router's WPS push button (stored in flash).
- **Wi-Fi test:** Inspect RSSI, connection time, and tune 802.11 protocol (`b/g/n`, `b/g`, `b only`) or TX power.

## Building and Flashing

This project uses [PlatformIO](https://platformio.org/).

```bash
# Build main firmware for M5Stack Core2
python -m platformio run -e core2

# Flash over USB
python -m platformio run -e core2 -t upload

# Open serial monitor (115200 baud)
python -m platformio device monitor -e core2
```

### Optional Fallback Wi-Fi Credentials

Normally you connect to Wi-Fi on the device via **Settings -> Connect Wi-Fi (WPS)**. If your router does not support WPS, copy `include/secrets.h.example` to `include/secrets.h` (which is git-ignored) and set `WIFI_SSID` and `WIFI_PASSWORD` before building.

### Configuration (`include/config.h`)

Key compile-time settings in [`include/config.h`](include/config.h):

| Macro | Default | Description |
|---|---|---|
| `HUEALARM_TZ` | `"CET-1CEST,M3.5.0,M10.5.0/3"` | POSIX timezone string |
| `HUEALARM_WAKE_MODE` | `HUEALARM_WAKE_TOUCH` | `HUEALARM_WAKE_TOUCH` (deep sleep, wake on screen touch, ~1 month battery) or `HUEALARM_WAKE_BUTTON` (power off, wake on side button, many months battery). See [`docs/POWER.md`](docs/POWER.md). |
| `HUEALARM_LAMP_MATCH` | `"Twilight"` | Default case-insensitive product/light name match before a room/zone is selected on-device |
| `HUEALARM_SCREEN_TIMEOUT_MS` | `5000` | Idle time before the screen turns off and the device goes back to sleep |
| `HUEALARM_BRIGHTNESS` | `80` | Display backlight brightness (`0`..`255`) |

### USB Serial Commands (Debugging)

While the device is awake and connected via USB serial (`115200` baud), send a single character:
- `d` — Dump the RTC ring-buffer log (survives deep sleep)
- `b` — Print PMIC / battery status
- `a` — Schedule a test alarm in 3 minutes (with a 1-minute sunrise)
- `x` — Disable the alarm
- `s` — Trigger an immediate NTP time sync

## Auxiliary Environments

Defined in [`platformio.ini`](platformio.ini):
- `env:powertest` (`src/powertest/main.cpp`) — Automated battery discharge and wake-up latency benchmark used to produce the figures in [`docs/POWER.md`](docs/POWER.md).
- `env:hwcheck` (`src/hwcheck/main.cpp`) — Hardware self-test for display, touch, speaker, RTC, IMU, I2C scan, and battery sensors.

## License

[MIT](LICENSE)
