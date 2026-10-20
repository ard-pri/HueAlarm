# Power, sleep and battery life

Hardware: M5Stack Core2 v1.1 (power chip AXP2101, current monitor INA3221, RTC
BM8563) with Battery Module 13.2 (1500 mAh) + bottom battery, about 1600 mAh.
Target: 3–4 months per charge, i.e. an average current of at most ~0.67 mA.

## The switch: `HUEALARM_WAKE_MODE` in `include/config.h`

| Mode | Between uses | Waking up | Time to the clock | Sleep current | Runtime (~1600 mAh) |
|---|---|---|---|---|---|
| `HUEALARM_WAKE_TOUCH` (current) | ESP32 deep sleep; only the LCD/touch supply stays on | touch the screen | ~0.35 s (+~0.2 s chip boot) | ~2 mA | ~1 month |
| `HUEALARM_WAKE_BUTTON` | completely off (power chip off) | short press on the side button | ~0.45 s (+~0.2 s chip boot) | ~0 | many months |

In both modes the RTC wakes / switches on the device for the alarm steps
(Wi-Fi + sunrise 90 s before the sunrise starts, ringing at the alarm time).
Change the define, rebuild and flash (`python -m platformio run -e core2 -t upload`).

Decision (2026-10): start with touch and watch how often it needs charging; switch
to the button if charging becomes a nuisance.

## Measurements

All with `env:powertest` (`src/powertest/main.cpp`): battery voltage measured under
identical conditions at the start and end of each block; ~1 mV/h ≈ 1.6 mA at ~4.0 V.

Round 1 (4 h blocks, ~4.05 V):

| Block | Mode | Drop | ≈ Current |
|---|---|---|---|
| A | light sleep (the first alarm firmware) | 2.25 mV/h | ~3.6 mA |
| B | deep sleep, touch wake, all supplies on | 1.25 mV/h | ~2 mA |
| C | deep sleep, all switchable supplies + INA3221 off (no touch wake) | 0.25 mV/h | ≤ 0.5 mA |
| D | power chip off, RTC switches on | ~0 | ~0 |

Round 2 (6 h blocks, ~4.02 V), touch wake verified in both:

| Block | Mode | Drop | ≈ Current |
|---|---|---|---|
| E | deep sleep + touch, everything off except the LCD/touch supply (ALDO2/ALDO4) | 1.67 mV/h | ~2.7 mA |
| F | E + touch controller in monitor mode | 1.33 mV/h | ~2.1 mA |

Conclusion: the LCD/touch supply (ALDO4 powers LCD, touch controller and SD slot
together) costs ~1.5–2 mA. Touch wake therefore tops out around one month; only
"off + side button" reaches the 3–4 month target.

Earlier, cruder measurements (voltage trend over nights, light sleep): ~8 mA with
the 5 V boost on, no measurable gain from switching it off. The AXP battery
percentage is unreliable; use the voltage trend.

## Start-up time (wake-up to clock on screen)

Measured with the latency commands of `env:powertest` (millis() at the drawn clock,
plus ~0.2–0.3 s ROM/bootloader before millis() starts):

| Change | Clock drawn after |
|---|---|
| default start-up | ~890 ms (setup() only starts at 508 ms) |
| skip the PSRAM memory test (`testSPIRAM()` override in `power.cpp`) | ~450 ms |
| + IMU / microphone not started | ~415 ms |
| + no LCD reset after deep sleep (`clear_display = false`) | ~345 ms |

After a power-on (button mode) the LCD must be cleared, otherwise it flashes grey:
~410–450 ms. The backlight stays off until the first frame is drawn.

## Other findings

- The ESP32's own clock (RC oscillator) drifts ~1 % while sleeping. The time is
  therefore always read from the RTC on start-up, long sleeps end 2 % early and
  the remainder is slept again. In button mode the RTC itself switches the device on.
- A long press on the Core2 side button restarts it (it does not switch it off).
- On USB the power chip cannot switch off; button mode falls back to deep sleep.
- Charging indicator: the AXP2101 charge status is right. The INA3221 "battery
  current" is not usable for it: on USB with a full battery (4.16 V, stable) it
  showed a constant ~168 mA.
- RTC timer power-on (block D) works on this Core2 v1.1.
- Wi-Fi: the Core2 connects reliably (~2 s); the CoreS3's antenna was too weak.
