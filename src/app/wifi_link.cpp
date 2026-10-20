#include "wifi_link.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "log.h"

// Wi-Fi credentials: received via WPS and stored in flash; include/secrets.h is
// only the fallback for a device that has not been connected with WPS yet.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

namespace wifilink {
namespace {

constexpr uint32_t kRetryIntervalMs = 4000;
constexpr const char* kPrefsNamespace = "wifi";

enum class State { Off, Connecting, Connected, Failed };

State state = State::Off;
int users = 0;
uint32_t connectStart = 0;
uint32_t lastAttempt = 0;
Settings current;
Stats stat;
bool eventsRegistered = false;
String ssid = WIFI_SSID;
String password = WIFI_PASSWORD;

const char* authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    default:                        return "other";
  }
}

// Diagnostics after a failed connect: list the 2.4 GHz networks the device can see.
void logVisibleNetworks() {
  int count = WiFi.scanNetworks();
  Log.printf("[wifi] %d networks visible:\n", count);
  bool exact = false, caseOnly = false;
  for (int i = 0; i < count; ++i) {
    String name = WiFi.SSID(i);
    exact |= name == ssid;
    caseOnly |= name.equalsIgnoreCase(ssid) && name != ssid;
    Log.printf("[wifi]   ch %2d  %4d dBm  %-10s %s%s\n", WiFi.channel(i), WiFi.RSSI(i),
                  authName(WiFi.encryptionType(i)), name.isEmpty() ? "(hidden)" : name.c_str(),
                  name.equalsIgnoreCase(ssid) ? "  <= configured" : "");
  }
  Log.printf("[wifi] configured SSID: %s\n",
                exact ? "visible" : caseOnly ? "visible but different upper/lower case" : "NOT visible");
  WiFi.scanDelete();
}

void registerEvents() {
  if (eventsRegistered) return;
  eventsRegistered = true;
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        // Our own disconnect() calls report ASSOC_LEAVE; keep the real cause.
        if (reason != WIFI_REASON_ASSOC_LEAVE) stat.lastReason = reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

void applyRadioSettings() {
  uint8_t protocols = WIFI_PROTOCOL_11B;
  if (current.protocol != Protocol::B) protocols |= WIFI_PROTOCOL_11G;
  if (current.protocol == Protocol::BGN) protocols |= WIFI_PROTOCOL_11N;
  esp_wifi_set_protocol(WIFI_IF_STA, protocols);
  if (current.protocol == Protocol::BGN) esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  WiFi.setTxPower((wifi_power_t)current.txPower);
  WiFi.setSleep(false);  // modem sleep drops handshake frames on weak links
}

void radioOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state = State::Off;
  Log.println("[wifi] off");
}

void startConnect() {
  if (ssid.isEmpty()) {
    Log.println("[wifi] no network configured");
    state = State::Failed;
    return;
  }
  registerEvents();
  if (WiFi.isConnected()) WiFi.disconnect();  // restart with new settings
  WiFi.mode(WIFI_STA);
  applyRadioSettings();
  WiFi.begin(ssid.c_str(), password.c_str());
  connectStart = lastAttempt = millis();
  stat = Stats{};
  stat.attempts = 1;
  state = State::Connecting;
  Log.printf("[wifi] connecting (%s, %.1f dBm)\n", protocolName(current.protocol),
                current.txPower / 4.0f);
}

}  // namespace

void request() {
  ++users;
  if (state == State::Off || state == State::Failed) startConnect();
}

void release() {
  if (users > 0) --users;
  if (users == 0 && state != State::Off) radioOff();
}

void update() {
  if (state != State::Connecting && state != State::Connected) return;

  wl_status_t status = WiFi.status();

  if (state == State::Connected) {
    if (status != WL_CONNECTED) {
      Log.printf("[wifi] connection lost (%s), reconnecting\n", reasonName(stat.lastReason));
      startConnect();
    }
    return;
  }

  stat.connectMs = millis() - connectStart;
  if (status == WL_CONNECTED) {
    state = State::Connected;
    Log.printf("[wifi] connected, IP %s, RSSI %d dBm, %lu ms, %u attempt(s)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned long)stat.connectMs,
                  stat.attempts);
  } else if (stat.connectMs > HUEALARM_WIFI_TIMEOUT_MS) {
    Log.printf("[wifi] connect timed out after %u attempts (%s)\n", stat.attempts,
                  reasonName(stat.lastReason));
    WiFi.disconnect();
    logVisibleNetworks();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    state = State::Failed;
  } else if ((status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) &&
             millis() - lastAttempt > kRetryIntervalMs) {
    // A failed handshake leaves the driver idle; start a fresh attempt.
    ++stat.attempts;
    Log.printf("[wifi] retry %u (%s)\n", stat.attempts, reasonName(stat.lastReason));
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
    lastAttempt = millis();
  }
}

bool connected() { return state == State::Connected; }

bool connecting() { return state == State::Connecting; }

bool failed() { return state == State::Failed; }

void loadSettings() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  current.protocol = (Protocol)prefs.getUChar("proto", (uint8_t)Settings{}.protocol);
  current.txPower = prefs.getChar("txpower", Settings{}.txPower);
  ssid = prefs.getString("ssid", WIFI_SSID);
  password = prefs.getString("pass", WIFI_PASSWORD);
  prefs.end();
  Log.printf("[wifi] settings: %s, %.1f dBm\n", protocolName(current.protocol),
                current.txPower / 4.0f);
}

void setCredentials(const String& newSsid, const String& newPassword) {
  ssid = newSsid;
  password = newPassword;
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  Log.printf("[wifi] stored network %s\n", ssid.c_str());
}

const String& networkName() { return ssid; }

const Settings& settings() { return current; }

void setSettings(const Settings& settings) {
  current = settings;
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putUChar("proto", (uint8_t)current.protocol);
  prefs.putChar("txpower", current.txPower);
  prefs.end();
  if (state == State::Connecting || state == State::Connected) startConnect();
}

const char* protocolName(Protocol protocol) {
  switch (protocol) {
    case Protocol::BGN: return "b/g/n";
    case Protocol::BG:  return "b/g";
    case Protocol::B:   return "b only";
  }
  return "?";
}

const Stats& stats() { return stat; }

const char* reasonName(uint8_t reason) {
  if (reason == 0) return "-";
  return WiFi.disconnectReasonName((wifi_err_reason_t)reason);
}

int rssi() { return state == State::Connected ? WiFi.RSSI() : 0; }

bool scanConfigured(int& rssiOut, int& channelOut) {
  bool radioWasOff = WiFi.getMode() == WIFI_OFF;
  if (radioWasOff) WiFi.mode(WIFI_STA);
  int count = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < count; ++i) {
    if (WiFi.SSID(i) != ssid) continue;
    if (!found || WiFi.RSSI(i) > rssiOut) {
      rssiOut = WiFi.RSSI(i);
      channelOut = WiFi.channel(i);
    }
    found = true;
  }
  WiFi.scanDelete();
  if (radioWasOff) WiFi.mode(WIFI_OFF);
  Log.printf("[wifi] scan: configured SSID %s", found ? "visible" : "not visible");
  if (found) Log.printf(", %d dBm, channel %d", rssiOut, channelOut);
  Log.println();
  return found;
}

}  // namespace wifilink
