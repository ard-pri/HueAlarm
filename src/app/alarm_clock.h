#pragma once

#include <Arduino.h>
#include <time.h>

// Daily alarm (one time, on/off) and its sequence: Wi-Fi is brought up shortly
// before the sunrise, the lamp's sunrise is started at (alarm - sunrise duration),
// and the speaker rings at the alarm time until stopped or snoozed.
//
// The device sleeps (deep sleep or power off) between the steps, so the sequence
// state is kept in flash and restored by begin().
namespace alarmclock {

struct Config {
  bool enabled = false;
  uint8_t hour = 7;
  uint8_t minute = 0;
  uint8_t sunriseMinutes = 10;  // 0 = no sunrise
  bool light = true;            // false = beep only, no sunrise

  // Sunrise duration actually used (0 when the light is switched off).
  uint8_t sunrise() const { return light ? sunriseMinutes : 0; }
};

enum class State { Off, Waiting, Preparing, Sunrise, Ringing, Snoozed };

void begin();   // load config and sequence state from flash
void update();  // drive the sequence; call from loop()

const Config& config();
void setConfig(const Config& config);  // stores and reschedules
void setEnabled(bool enabled);
void setLight(bool light);  // light + beep, or beep only

State state();
const char* stateName(State state);
bool isRinging();

// Next alarm time and its sunrise start (0 if the alarm is off or the clock is not set).
time_t nextAlarm();
time_t sunriseStart();

// When the alarm next needs the CPU (0 = not before a touch), and whether the
// device may sleep right now (not while preparing the sunrise or ringing).
time_t nextEvent();
bool canSleep();

void snooze();
void stop();

}  // namespace alarmclock
