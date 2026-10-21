// HueAlarm - a simple bedside alarm clock (Braun BC08 style) that starts the Hue
// lamp's sunrise before the alarm.
//
// Clock screen: tap the bell to switch the alarm on / off, long-press it for light +
// beep or beep only, tap the alarm time to set it (scroll wheels), long-press
// anywhere else for the settings.
// Ringing: tap anywhere to snooze, tap the bell to stop.
//
// Between uses the device deep-sleeps or switches off (HUEALARM_WAKE_MODE in
// config.h, see docs/POWER.md); every wake-up restarts the firmware, so setup()
// draws the clock first and does everything else afterwards.

#include <M5Unified.h>
#include <time.h>

#include "alarm_clock.h"
#include "alarm_screen.h"
#include "clock_face.h"
#include "config.h"
#include "hue_screen.h"
#include "log.h"
#include "power.h"
#include "settings_screen.h"
#include "time_sync.h"
#include "wifi_link.h"
#include "wifi_screen.h"
#include "wps_screen.h"

namespace {

constexpr uint32_t kSettingTimeoutMs = 20000;  // alarm / settings screens
constexpr uint32_t kTestTimeoutMs = 120000;    // Wi-Fi / Hue test screens
constexpr uint32_t kSerialAwakeMs = 10000;     // stay awake after serial input (debugging)

enum class Screen { Clock, SetAlarm, Settings, Wps, Wifi, Hue, Ringing };

M5Canvas canvas(&M5.Display);
Screen screen = Screen::Clock;
uint32_t lastActivity = 0;  // millis() of the last touch
uint32_t lastSerial = 0;    // millis() of the last serial input
bool ignoreTouch = false;   // the touch that woke the device is not a tap

void drawClock() {
  clockface::draw(canvas, time(nullptr));
  canvas.pushSprite(0, 0);
}

void wakeScreen() {
  screen = Screen::Clock;
  drawClock();
  power::screenOn();
  lastActivity = millis();
  ignoreTouch = true;
}

void leaveScreen() {
  if (screen == Screen::Hue) huescreen::exit();
  if (screen == Screen::Wifi) wifiscreen::exit();
  if (screen == Screen::Wps) wpsscreen::exit();
  screen = Screen::Clock;
}

uint32_t screenTimeout() {
  switch (screen) {
    case Screen::Clock:    return HUEALARM_SCREEN_TIMEOUT_MS;
    case Screen::SetAlarm:
    case Screen::Settings: return kSettingTimeoutMs;
    case Screen::Ringing:  return UINT32_MAX;
    default:               return kTestTimeoutMs;
  }
}

// The device may sleep when nothing runs in the background: no Wi-Fi, no time
// sync, no sunrise preparation or ringing.
bool maySleep() {
  auto sync = timesync::state();
  return alarmclock::canSleep() && !wifilink::connected() && !wifilink::connecting() &&
         sync != timesync::State::Connecting && sync != timesync::State::Syncing &&
         millis() - lastSerial > kSerialAwakeMs;
}

void updateClock() {
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail(0);
    if (t.wasHold()) {
      if (clockface::isBell(t.x, t.y)) {
        // Long press on the bell: light + beep, or beep only.
        alarmclock::setLight(!alarmclock::config().light);
        return;
      }
      screen = Screen::Settings;
      settingsscreen::enter();
      return;
    }
    if (t.wasClicked()) {
      if (clockface::isAlarmTime(t.x, t.y)) {
        screen = Screen::SetAlarm;
        alarmscreen::enter();
        return;
      }
      if (clockface::isBell(t.x, t.y)) alarmclock::setEnabled(!alarmclock::config().enabled);
    }
  }
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 250) {
    lastDraw = millis();
    drawClock();
  }
}

void updateRinging() {
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail(0);
    // React on release, so the lifting finger does not tap the next screen.
    if (t.wasClicked()) {
      if (clockface::isBell(t.x, t.y)) {
        alarmclock::stop();
      } else {
        alarmclock::snooze();
      }
    }
  }
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 100) {
    lastDraw = millis();
    clockface::drawRinging(canvas, time(nullptr));
    canvas.pushSprite(0, 0);
  }
}

// Serial commands for testing over USB (only while awake):
// d = dump log, b = power status, a = test alarm in 3 min (1 min sunrise), x = alarm off,
// s = sync the time now (tests the Wi-Fi).
void handleSerial() {
  while (Serial.available()) {
    char command = Serial.read();
    lastSerial = millis();
    if (command == 'd') {
      Log.dump(Serial);
    } else if (command == 'b') {
      power::logStatus();
    } else if (command == 'a') {
      time_t at = time(nullptr) + 180;
      tm local;
      localtime_r(&at, &local);
      alarmclock::Config test = alarmclock::config();
      test.enabled = true;
      test.hour = local.tm_hour;
      test.minute = local.tm_min;
      test.sunriseMinutes = 1;
      alarmclock::setConfig(test);
    } else if (command == 'x') {
      alarmclock::setEnabled(false);
    } else if (command == 's') {
      timesync::start();
    }
  }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  power::configure(cfg);
  M5.begin(cfg);
  power::begin();
  timesync::begin();  // system clock from the RTC
  power::Boot boot = power::bootReason();

  canvas.setPsram(true);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  alarmclock::begin();

  // Show the clock first; a timer wake-up works with the screen off.
  if (boot != power::Boot::Timer) wakeScreen();

  Log.printf("[boot] %s, clock shown after %lu ms\n", power::bootName(boot),
             (unsigned long)millis());
  wifilink::loadSettings();
  if (timesync::syncDue()) timesync::start();
}

void loop() {
  M5.update();
  handleSerial();
  wifilink::update();
  timesync::update();
  alarmclock::update();

  if (alarmclock::isRinging() && screen != Screen::Ringing) {
    leaveScreen();
    screen = Screen::Ringing;
    power::screenOn();
  } else if (!alarmclock::isRinging() && screen == Screen::Ringing) {
    screen = Screen::Clock;
    lastActivity = millis();
  }

  bool touching = M5.Touch.getCount() > 0;
  if (touching) lastActivity = millis();
  if (touching && !power::isScreenOn()) wakeScreen();  // touched during a timer wake-up
  bool skipTouch = ignoreTouch;
  if (!touching) ignoreTouch = false;

  if (power::isScreenOn() && millis() - lastActivity > screenTimeout()) {
    leaveScreen();
    power::screenOff();
  }

  if (!power::isScreenOn()) {
    if (maySleep()) power::sleepUntil(alarmclock::nextEvent());  // does not return
    delay(10);
    return;
  }

  if (skipTouch && screen == Screen::Clock) {
    drawClock();
  } else if (screen == Screen::Ringing) {
    updateRinging();
  } else if (screen == Screen::SetAlarm) {
    if (!alarmscreen::update(canvas)) screen = Screen::Clock;
  } else if (screen == Screen::Settings) {
    switch (settingsscreen::update(canvas)) {
      case settingsscreen::Action::Back:     screen = Screen::Clock; break;
      case settingsscreen::Action::OpenWps:  screen = Screen::Wps; wpsscreen::enter(); break;
      case settingsscreen::Action::OpenWifi: screen = Screen::Wifi; wifiscreen::enter(); break;
      case settingsscreen::Action::OpenHue:  screen = Screen::Hue; huescreen::enter(); break;
      case settingsscreen::Action::None:     break;
    }
  } else if (screen == Screen::Wps) {
    if (wpsscreen::active()) lastActivity = millis();  // stay on while waiting for the router
    if (!wpsscreen::update(canvas)) {
      wpsscreen::exit();
      screen = Screen::Settings;
    }
  } else if (screen == Screen::Hue) {
    if (!huescreen::update(canvas)) {
      huescreen::exit();
      screen = Screen::Settings;
    }
  } else if (screen == Screen::Wifi) {
    if (!wifiscreen::update(canvas)) {
      wifiscreen::exit();
      screen = Screen::Settings;
    }
  } else {
    updateClock();
  }
  delay(10);
}
