#include "alarm_clock.h"

#include <M5Unified.h>
#include <Preferences.h>

#include <vector>

#include "config.h"
#include "hue.h"
#include "log.h"
#include "time_sync.h"
#include "wifi_link.h"

namespace alarmclock {
namespace {

constexpr const char* kPrefsNamespace = "alarm";
constexpr time_t kWifiLeadSeconds = 90;      // connect this long before the sunrise starts
constexpr time_t kSunriseRetrySeconds = 15;  // retry interval when starting the sunrise fails
constexpr time_t kSunriseGiveUpSeconds = 30; // stop trying this close to the alarm
constexpr int kMaxWifiFailures = 2;          // connect attempts (each with retries) per alarm
constexpr time_t kSnoozeSeconds = 9 * 60;
constexpr time_t kRingMaxSeconds = 10 * 60;  // ringing stops by itself after this
constexpr time_t kLateRingSeconds = 5 * 60;  // a missed alarm still rings if this late or less

Config cfg;
State current = State::Off;
time_t alarmAt = 0;        // alarm being handled / next alarm
time_t ringUntil = 0;
time_t snoozeUntil = 0;
time_t lastSunriseTry = 0;
bool holdingWifi = false;
uint32_t ringStartMs = 0;
bool lampResolved = false;
int wifiFailures = 0;
std::vector<hue::Light> lamp;  // lamp lights still waiting for the sunrise command

// The sequence state survives deep sleep / power off in flash. Preparing and
// Ringing are never slept through, so they are not stored.
void saveSequence() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putLong("seqAt", (long)alarmAt);
  prefs.putUChar("seqState", (uint8_t)current);
  prefs.putLong("seqSnooze", (long)snoozeUntil);
  prefs.end();
}

void setState(State next) {
  if (next == current) return;
  current = next;
  Log.printf("[alarm] %s\n", stateName(next));
  if (next != State::Preparing && next != State::Ringing) saveSequence();
}

void releaseWifi() {
  if (holdingWifi) wifilink::release();
  holdingWifi = false;
}

// First alarm time strictly after `after` (the alarm rings every day).
time_t nextAfter(time_t after) {
  tm day;
  localtime_r(&after, &day);
  for (int i = 0; i <= 1; ++i) {
    tm candidate = day;
    candidate.tm_mday += i;
    candidate.tm_hour = cfg.hour;
    candidate.tm_min = cfg.minute;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    time_t t = mktime(&candidate);  // normalizes the date
    if (t > after) return t;
  }
  return 0;
}

void logSchedule() {
  if (!alarmAt) {
    Log.println("[alarm] no alarm scheduled");
    return;
  }
  char text[32];
  tm local;
  localtime_r(&alarmAt, &local);
  strftime(text, sizeof(text), "%a %d.%m %H:%M", &local);
  Log.printf("[alarm] next alarm %s, sunrise %u min before\n", text, cfg.sunrise());
}

void schedule(time_t after) {
  releaseWifi();
  if (!cfg.enabled || !timesync::hasValidTime()) {
    alarmAt = 0;
    setState(State::Off);
    saveSequence();
    return;
  }
  alarmAt = nextAfter(after);
  setState(alarmAt ? State::Waiting : State::Off);
  saveSequence();
  logSchedule();
}

// Look up the lamp's lights ahead of time, so the sunrise can start on the second.
bool resolveLamp() {
  hue::loadCredentials();
  if (hue::findLights(HUEALARM_LAMP_MATCH, lamp) != hue::Result::Ok || lamp.empty()) {
    Log.printf("[alarm] lamp lookup failed: %s\n", hue::lastError().c_str());
    lamp.clear();
    return false;
  }
  return true;
}

// Start the sunrise on the lamp lights; it ends at the alarm time. Lights that
// started are removed from `lamp`, so retries only address the remaining ones.
// The Twilight ignores a sunrise while it is off, so "on" is sent in the same
// request (switching on separately first makes the lamp flash).
bool startSunrise() {
  for (auto it = lamp.begin(); it != lamp.end();) {
    uint32_t durationMs = (uint32_t)(alarmAt - time(nullptr)) * 1000;
    if (hue::startSunrise(it->id, durationMs, true) == hue::Result::Ok) {
      Log.printf("[alarm] sunrise started on %s (%lu s)\n", it->name.c_str(),
                 (unsigned long)(durationMs / 1000));
      it = lamp.erase(it);
    } else {
      ++it;
    }
  }
  return lamp.empty();
}

void startBeeping();

void startRinging(time_t now) {
  releaseWifi();
  ringUntil = now + kRingMaxSeconds;
  ringStartMs = millis();
  setState(State::Ringing);
  startBeeping();
}

// The alarm sound is one pre-rendered cycle - 4 beeps, then a pause - that the
// speaker repeats on its own, so the rhythm stays exact while the CPU draws.
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kToneHz = 2000;
constexpr uint32_t kBeepMs = 100;
constexpr uint32_t kBeepGapMs = 100;
constexpr uint32_t kBeepCount = 4;
constexpr uint32_t kPauseMs = 600;
constexpr uint32_t kCycleMs = kBeepCount * (kBeepMs + kBeepGapMs) + kPauseMs;
constexpr size_t kCycleSamples = kSampleRate * kCycleMs / 1000;
uint8_t beepCycle[kCycleSamples];

void startBeeping() {
  static bool built = false;
  if (!built) {
    built = true;
    for (size_t i = 0; i < kCycleSamples; ++i) {
      uint32_t ms = i * 1000 / kSampleRate;
      bool inBeep = ms < kBeepCount * (kBeepMs + kBeepGapMs) && ms % (kBeepMs + kBeepGapMs) < kBeepMs;
      bool high = (i * kToneHz * 2 / kSampleRate) % 2;  // square wave
      beepCycle[i] = inBeep ? (high ? 236 : 20) : 128;   // unsigned 8 bit, 128 = silence
    }
  }
  M5.Speaker.setVolume(64);
  // ringing stops after kRingMaxSeconds anyway, so this repeat count is "forever"
  M5.Speaker.playRaw(beepCycle, kCycleSamples, kSampleRate, false, 1000, 0, true);
}

// Volume rises over the first minute: 64 -> 255.
void rampVolume() {
  uint32_t ramp = min<uint32_t>(millis() - ringStartMs, 60000);
  M5.Speaker.setVolume(64 + ramp * 191 / 60000);
}

// A step was due while the device was not running (e.g. empty battery).
bool tooLate(time_t now) {
  if (now < alarmAt + kLateRingSeconds) return false;
  Log.println("[alarm] alarm time passed while not running, skipping");
  schedule(now);
  return true;
}

}  // namespace

void begin() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  cfg.enabled = prefs.getBool("enabled", cfg.enabled);
  cfg.hour = prefs.getUChar("hour", cfg.hour);
  cfg.minute = prefs.getUChar("minute", cfg.minute);
  cfg.sunriseMinutes = prefs.getUChar("sunrise", cfg.sunriseMinutes);
  cfg.light = prefs.getBool("light", cfg.light);
  time_t storedAt = (time_t)prefs.getLong("seqAt", 0);
  State storedState = (State)prefs.getUChar("seqState", (uint8_t)State::Off);
  snoozeUntil = (time_t)prefs.getLong("seqSnooze", 0);
  prefs.end();

  // Continue a sequence that was interrupted by sleep, otherwise schedule afresh.
  bool resumable = cfg.enabled && storedAt && timesync::hasValidTime() &&
                   (storedState == State::Waiting || storedState == State::Sunrise ||
                    storedState == State::Snoozed);
  if (resumable) {
    alarmAt = storedAt;
    current = storedState;
  } else {
    schedule(time(nullptr));
  }
}

void update() {
  time_t now = time(nullptr);

  // The clock may become valid only after the first time sync.
  if (current == State::Off && cfg.enabled && !alarmAt && timesync::hasValidTime()) {
    schedule(now);
  }

  switch (current) {
    case State::Off:
      break;

    case State::Waiting:
      if (tooLate(now)) break;
      if (now >= alarmAt) {
        startRinging(now);
      } else if (cfg.sunrise() > 0 && now >= sunriseStart() - kWifiLeadSeconds) {
        wifilink::request();
        holdingWifi = true;
        lastSunriseTry = 0;
        lampResolved = false;
        wifiFailures = 0;
        timesync::start();  // Wi-Fi is up anyway: keep the clock exact
        setState(State::Preparing);
      }
      break;

    case State::Preparing:
      if (now >= alarmAt) {
        startRinging(now);
      } else if (now >= alarmAt - kSunriseGiveUpSeconds) {
        Log.println("[alarm] could not start the sunrise in time");
        releaseWifi();
        setState(State::Sunrise);
      } else if (wifilink::failed()) {
        // Each failed connect already took the full timeout with retries; with the
        // network down, retrying until the alarm would cost more than a night of sleep.
        if (++wifiFailures >= kMaxWifiFailures) {
          Log.println("[alarm] Wi-Fi unavailable, alarm without sunrise");
          releaseWifi();
          setState(State::Sunrise);
        } else {
          wifilink::release();  // try connecting again
          wifilink::request();
        }
      } else if (wifilink::connected() && now - lastSunriseTry >= kSunriseRetrySeconds) {
        if (!lampResolved) {
          // Resolve the lamp during the lead time, then wait for the sunrise start.
          lastSunriseTry = now;
          lampResolved = resolveLamp();
        } else if (now >= sunriseStart()) {
          lastSunriseTry = now;
          if (startSunrise()) {
            releaseWifi();
            setState(State::Sunrise);
          }
        }
      }
      break;

    case State::Sunrise:
      if (tooLate(now)) break;
      if (now >= alarmAt) startRinging(now);
      break;

    case State::Ringing:
      if (now >= ringUntil) {
        Log.println("[alarm] ringing timed out");
        stop();
      } else {
        rampVolume();
      }
      break;

    case State::Snoozed:
      if (now >= snoozeUntil) startRinging(now);
      break;
  }
}

const Config& config() { return cfg; }

void setConfig(const Config& config) {
  cfg = config;
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putBool("enabled", cfg.enabled);
  prefs.putUChar("hour", cfg.hour);
  prefs.putUChar("minute", cfg.minute);
  prefs.putUChar("sunrise", cfg.sunriseMinutes);
  prefs.putBool("light", cfg.light);
  prefs.end();
  if (current == State::Ringing || current == State::Snoozed) {
    if (!cfg.enabled) stop();  // switching the alarm off also ends ringing / snooze
    return;
  }
  schedule(time(nullptr));
}

void setEnabled(bool enabled) {
  Config changed = cfg;
  changed.enabled = enabled;
  setConfig(changed);
}

void setLight(bool light) {
  Config changed = cfg;
  changed.light = light;
  setConfig(changed);
}

State state() { return current; }

const char* stateName(State state) {
  switch (state) {
    case State::Off:       return "off";
    case State::Waiting:   return "waiting";
    case State::Preparing: return "preparing sunrise";
    case State::Sunrise:   return "sunrise running";
    case State::Ringing:   return "ringing";
    case State::Snoozed:   return "snoozed";
  }
  return "?";
}

bool isRinging() { return current == State::Ringing; }

time_t nextAlarm() { return alarmAt; }

time_t sunriseStart() { return alarmAt ? alarmAt - (time_t)cfg.sunrise() * 60 : 0; }

time_t nextEvent() {
  switch (current) {
    case State::Waiting:
      return cfg.sunrise() ? sunriseStart() - kWifiLeadSeconds : alarmAt;
    case State::Sunrise: return alarmAt;
    case State::Snoozed: return snoozeUntil;
    default:             return 0;
  }
}

bool canSleep() { return current != State::Preparing && current != State::Ringing; }

void snooze() {
  if (current != State::Ringing) return;
  M5.Speaker.stop();
  snoozeUntil = time(nullptr) + kSnoozeSeconds;
  setState(State::Snoozed);
}

void stop() {
  if (current != State::Ringing && current != State::Snoozed) return;
  M5.Speaker.stop();
  schedule(alarmAt);  // next occurrence after the one just handled
}

}  // namespace alarmclock
