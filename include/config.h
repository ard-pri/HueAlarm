#pragma once

// POSIX timezone string. Default: Central Europe (CET/CEST).
// Others: "GMT0BST,M3.5.0/1,M10.5.0" (UK), "EST5EDT,M3.2.0,M11.1.0" (US East).
#define HUEALARM_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

#define HUEALARM_NTP_SERVER_1 "pool.ntp.org"
#define HUEALARM_NTP_SERVER_2 "time.google.com"

// The Hue lamp the alarm controls: matched (case-insensitive) against the
// device product name ("Hue Twilight") and the device / light names.
#define HUEALARM_LAMP_MATCH "Twilight"

// Give up connecting / syncing after this many milliseconds.
#define HUEALARM_WIFI_TIMEOUT_MS 30000

// How the device sleeps and wakes up - the main battery-life trade-off.
// Measurements and background: docs/POWER.md
//
//   HUEALARM_WAKE_TOUCH   deep sleep; touching the screen wakes it (~0.35 s to the
//                         clock). Sleep current ~2 mA: about 1 month per charge
//                         with ~1600 mAh.
//   HUEALARM_WAKE_BUTTON  completely off; the side button switches it on
//                         (~0.45 s to the clock), the RTC switches it on for the
//                         alarm. Sleep current ~0: many months per charge.
#define HUEALARM_WAKE_TOUCH 1
#define HUEALARM_WAKE_BUTTON 2
#define HUEALARM_WAKE_MODE HUEALARM_WAKE_TOUCH

// Screen switches off this long after the last touch.
#define HUEALARM_SCREEN_TIMEOUT_MS 5000

// Backlight brightness while the screen is on (0..255); mid bedroom level.
#define HUEALARM_BRIGHTNESS 60
