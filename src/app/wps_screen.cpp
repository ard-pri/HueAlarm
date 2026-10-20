#include "wps_screen.h"

#include <WiFi.h>
#include <esp_wps.h>

#include "log.h"
#include "wifi_link.h"

namespace wpsscreen {
namespace {

constexpr uint32_t kWindowMs = 120000;  // routers keep WPS open for about 2 minutes
constexpr uint32_t kConnectMs = 20000;
constexpr int kHeaderHeight = 40;
const uint16_t kAccent = lgfx::color565(64, 190, 84);
const uint16_t kText = lgfx::color565(204, 202, 172);

enum class State { Idle, Waiting, Connecting, Done, Failed };

State state = State::Idle;
uint32_t stateSince = 0;
volatile bool wpsSuccess = false;  // set from the Wi-Fi event task
volatile bool wpsRetry = false;
String failure;
esp_wps_config_t config;
bool eventsRegistered = false;

void setState(State next) {
  state = next;
  stateSince = millis();
}

void wpsStart() {
  config.wps_type = WPS_TYPE_PBC;
  strcpy(config.factory_info.manufacturer, "M5Stack");
  strcpy(config.factory_info.model_number, "Core2");
  strcpy(config.factory_info.model_name, "HueAlarm");
  strcpy(config.factory_info.device_name, "HueAlarm clock");
  if (esp_wifi_wps_enable(&config) != ESP_OK || esp_wifi_wps_start(0) != ESP_OK) {
    failure = "WPS could not start";
    setState(State::Failed);
  }
}

void radioOff() {
  esp_wifi_wps_disable();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void fail(const String& text) {
  failure = text;
  Log.printf("[wps] %s\n", text.c_str());
  radioOff();
  setState(State::Failed);
}

void start() {
  if (wifilink::connected() || wifilink::connecting()) {
    fail("Wi-Fi busy, try again");
    return;
  }
  if (!eventsRegistered) {
    eventsRegistered = true;
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t) {
      if (event == ARDUINO_EVENT_WPS_ER_SUCCESS) wpsSuccess = true;
      if (event == ARDUINO_EVENT_WPS_ER_FAILED || event == ARDUINO_EVENT_WPS_ER_TIMEOUT) wpsRetry = true;
    });
  }
  wpsSuccess = wpsRetry = false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wpsStart();
  if (state != State::Failed) {
    Log.println("[wps] waiting for the router's WPS button");
    setState(State::Waiting);
  }
}

void advance() {
  switch (state) {
    case State::Waiting:
      if (wpsSuccess) {
        // The credentials are now in the Wi-Fi driver; connect with them.
        esp_wifi_wps_disable();
        WiFi.begin();
        setState(State::Connecting);
      } else if (millis() - stateSince > kWindowMs) {
        fail("No WPS button pressed");
      } else if (wpsRetry) {
        // The router answered but not (yet) in WPS mode: keep listening.
        wpsRetry = false;
        esp_wifi_wps_disable();
        wpsStart();
      }
      break;

    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        String ssid = WiFi.SSID();
        wifilink::setCredentials(ssid, WiFi.psk());
        Log.printf("[wps] connected to %s, %d dBm\n", ssid.c_str(), WiFi.RSSI());
        radioOff();
        setState(State::Done);
      } else if (millis() - stateSince > kConnectMs) {
        fail("Received network, but could not connect");
      }
      break;

    default:
      break;
  }
}

void drawLines(M5Canvas& c, const String& line1, const String& line2, uint16_t color) {
  const int w = c.width();
  c.setTextDatum(middle_center);
  c.setFont(&fonts::FreeSans12pt7b);
  c.setTextColor(color);
  c.drawString(line1, w / 2, 110);
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextColor(TFT_LIGHTGREY);
  c.drawString(line2, w / 2, 145);
}

void draw(M5Canvas& c) {
  const int w = c.width();
  c.fillScreen(TFT_BLACK);
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(TFT_LIGHTGREY);
  c.drawString("< Back", 12, kHeaderHeight / 2);
  c.setTextDatum(middle_center);
  c.setTextColor(TFT_DARKGREY);
  c.drawString("Connect Wi-Fi (WPS)", w / 2, kHeaderHeight / 2);
  c.drawFastHLine(0, kHeaderHeight, w, lgfx::color565(40, 40, 40));

  switch (state) {
    case State::Waiting: {
      int left = (int)(kWindowMs - (millis() - stateSince)) / 1000;
      drawLines(c, "Press the WPS button", String("on the router (") + left + " s)", kText);
      break;
    }
    case State::Connecting:
      drawLines(c, "Connecting ...", "", kText);
      break;
    case State::Done:
      drawLines(c, "Connected", wifilink::networkName(), kAccent);
      break;
    case State::Failed:
      drawLines(c, failure, "Tap to try again", kText);
      break;
    case State::Idle:
      break;
  }
}

}  // namespace

void enter() { start(); }

void exit() {
  if (state == State::Waiting || state == State::Connecting) radioOff();
  setState(State::Idle);
}

bool active() { return state == State::Waiting || state == State::Connecting; }

bool update(M5Canvas& canvas) {
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail(0);
    if (t.wasClicked()) {
      if (t.y < kHeaderHeight && t.x < 110) return false;
      if (state == State::Failed || state == State::Done) start();
    }
  }
  advance();
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 250) {
    lastDraw = millis();
    draw(canvas);
    canvas.pushSprite(0, 0);
  }
  return true;
}

}  // namespace wpsscreen
