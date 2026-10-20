#pragma once

#include <Arduino.h>

// Shared Wi-Fi connection. Users request() it while they need the network and
// release() it afterwards; the radio is switched off when nobody needs it.
namespace wifilink {

void request();
void release();

// Drive connect / retry logic. Call from loop().
void update();

bool connected();
bool connecting();
// The current connect attempt timed out. Callers should release() and report the error.
bool failed();

// --- Radio settings (persisted in flash, applied on the next connect) ---

// Allowed 802.11 standards. Older, slower standards tolerate weaker signals.
enum class Protocol : uint8_t { BGN, BG, B };

struct Settings {
  Protocol protocol = Protocol::BGN;
  int8_t txPower = 78;  // wifi_power_t units (0.25 dBm), 78 = 19.5 dBm
};

void loadSettings();  // radio settings and the stored network (call once at start-up)

// Network credentials, e.g. received via WPS; stored in flash and used from the
// next connect on (include/secrets.h is only the fallback).
void setCredentials(const String& ssid, const String& password);
const String& networkName();
const Settings& settings();
// Store new settings. An active connection is restarted with them.
void setSettings(const Settings& settings);
const char* protocolName(Protocol protocol);

// --- Diagnostics ---

struct Stats {
  unsigned attempts = 0;
  uint32_t connectMs = 0;   // duration of the current / last connect
  uint8_t lastReason = 0;   // last disconnect reason (wifi_err_reason_t), 0 = none
};

const Stats& stats();
const char* reasonName(uint8_t reason);
int rssi();  // dBm while connected, 0 otherwise

// Blocking scan for the configured SSID (strongest access point). Returns false if not visible.
bool scanConfigured(int& rssiOut, int& channelOut);

}  // namespace wifilink
