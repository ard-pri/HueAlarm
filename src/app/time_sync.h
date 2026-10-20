#pragma once

// NTP time sync over the shared Wi-Fi link. The RTC keeps UTC; the local
// timezone is applied via TZ. The Wi-Fi link is released once the sync is done.
namespace timesync {

enum class State { Idle, Connecting, Syncing, Synced, Failed };

// Apply the timezone and load the system clock from the RTC. Call once after M5.begin().
void begin();

// Set the system clock from the RTC (crystal-accurate) and re-apply the timezone.
// The ESP32 system clock drifts by about 1 % while sleeping.
// Returns the correction in seconds.
int loadFromRtc();

// Start a non-blocking Wi-Fi connect + NTP sync.
void start();

// Drive the sync state machine. Call from loop().
void update();

State state();
const char* stateName(State state);

// True once the system clock holds a plausible date (from RTC or NTP).
bool hasValidTime();

// True if the clock is not set or the last successful sync is over a week old
// (at most one attempt every 6 hours, so a Wi-Fi outage does not cost battery).
// (Each alarm morning syncs anyway, while Wi-Fi is up for the sunrise.)
bool syncDue();

}  // namespace timesync
