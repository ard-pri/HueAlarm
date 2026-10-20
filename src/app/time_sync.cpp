#include "time_sync.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "log.h"
#include "wifi_link.h"

namespace timesync {
namespace {

constexpr time_t kMinValidEpoch = 1700000000;  // 2023-11-14, anything earlier is an unset clock
constexpr time_t kResyncSeconds = 7 * 24 * 3600;  // the RTC crystal drifts a few seconds a week
constexpr time_t kRetrySeconds = 6 * 3600;        // after a failed attempt (e.g. Wi-Fi down)

State currentState = State::Idle;
uint32_t stateSince = 0;
volatile bool ntpSynced = false;

void setState(State next) {
  currentState = next;
  stateSince = millis();
  Log.printf("[time] %s\n", stateName(next));
}

void onNtpSync(struct timeval*) { ntpSynced = true; }

void writeRtcFromSystemTime() {
  time_t now = time(nullptr);
  tm utc;
  gmtime_r(&now, &utc);
  M5.Rtc.setDateTime(&utc);
  Preferences prefs;
  prefs.begin("time", false);
  prefs.putLong("lastSync", (long)now);
  prefs.end();
}

void finish(State result) {
  sntp_stop();
  wifilink::release();
  setState(result);
}

}  // namespace

void begin() {
  loadFromRtc();
  Log.printf("[time] clock from RTC is %s\n", hasValidTime() ? "valid" : "not set");
}

int loadFromRtc() {
  time_t before = time(nullptr);
  if (M5.Rtc.isEnabled()) M5.Rtc.setSystemTimeFromRtc();
  // Set the timezone afterwards: setSystemTimeFromRtc() switches TZ to GMT0 and
  // restores it from a getenv() pointer that its own setenv() has invalidated.
  setenv("TZ", HUEALARM_TZ, 1);
  tzset();
  return (int)(time(nullptr) - before);
}

void start() {
  if (currentState == State::Connecting || currentState == State::Syncing) return;
  ntpSynced = false;
  Preferences prefs;
  prefs.begin("time", false);
  prefs.putLong("lastTry", (long)time(nullptr));
  prefs.end();
  wifilink::request();
  setState(State::Connecting);
}

void update() {
  switch (currentState) {
    case State::Connecting:
      if (wifilink::connected()) {
        sntp_set_time_sync_notification_cb(onNtpSync);
        configTzTime(HUEALARM_TZ, HUEALARM_NTP_SERVER_1, HUEALARM_NTP_SERVER_2);
        setState(State::Syncing);
      } else if (wifilink::failed()) {
        finish(State::Failed);
      }
      break;

    case State::Syncing:
      if (ntpSynced) {
        writeRtcFromSystemTime();
        finish(State::Synced);
      } else if (millis() - stateSince > HUEALARM_WIFI_TIMEOUT_MS) {
        Log.println("[time] NTP sync timed out");
        finish(State::Failed);
      }
      break;

    default:
      break;
  }
}

State state() { return currentState; }

const char* stateName(State state) {
  switch (state) {
    case State::Idle:       return "idle";
    case State::Connecting: return "connecting Wi-Fi";
    case State::Syncing:    return "syncing time";
    case State::Synced:     return "time synced";
    case State::Failed:     return "sync failed";
  }
  return "?";
}

bool syncDue() {
  Preferences prefs;
  prefs.begin("time", true);
  time_t lastSync = (time_t)prefs.getLong("lastSync", 0);
  time_t lastTry = (time_t)prefs.getLong("lastTry", 0);
  prefs.end();
  time_t now = time(nullptr);
  // Every wake-up is a start, so failed attempts must not repeat on each one.
  if (hasValidTime() && now - lastTry < kRetrySeconds) return false;
  return !hasValidTime() || now - lastSync > kResyncSeconds;
}

bool hasValidTime() { return time(nullptr) > kMinValidEpoch; }

}  // namespace timesync
