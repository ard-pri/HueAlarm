#pragma once

#include <M5Unified.h>
#include <time.h>

// Start-up, screen and sleep. Between uses the device either deep-sleeps (touch
// wakes it) or switches completely off (the side button switches it on), see
// HUEALARM_WAKE_MODE in config.h and docs/POWER.md. Both restart the firmware on
// wake-up, so start-up is tuned to show the clock as fast as possible.
namespace power {

enum class Boot {
  Touch,    // touch woke the device from deep sleep
  Button,   // switched on with the side button (or first power-up)
  Timer,    // woken for a scheduled alarm step (deep sleep timer or RTC alarm)
  Restart,  // reset, flashing, crash
};

// Fast start-up settings; call before M5.begin().
void configure(m5::M5Unified::config_t& cfg);

// After M5.begin(): keeps the backlight off until the first frame is drawn.
void begin();

Boot bootReason();
const char* bootName(Boot boot);

void screenOn();   // backlight on (draw the first frame before calling this)
void screenOff();
bool isScreenOn();

// Sleep until `wakeAt` (epoch seconds, 0 = until touch / button). Does not
// return: the firmware restarts on wake-up.
[[noreturn]] void sleepUntil(time_t wakeAt);

// Log power chip, battery and USB readings (diagnostics).
void logStatus();

}  // namespace power
