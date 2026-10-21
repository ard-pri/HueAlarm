#include "settings_screen.h"

#include "alarm_clock.h"
#include "config.h"
#include "hue.h"
#include "time_sync.h"
#include "wifi_link.h"

namespace settingsscreen {
namespace {

constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 33;
constexpr uint8_t kSunriseChoices[] = {0, 5, 10, 15, 20, 30};
const uint16_t kAccent = lgfx::color565(64, 190, 84);   // Braun bell green
const uint16_t kText = lgfx::color565(184, 180, 144);   // warm backlit LCD tone
const uint16_t kLine = lgfx::color565(40, 40, 40);

enum Row { kSunrise, kLight, kSync, kWps, kWifi, kInfo, kRowCount };

uint32_t lastDraw = 0;

uint8_t nextSunrise(uint8_t value) {
  for (size_t i = 0; i < sizeof(kSunriseChoices); ++i) {
    if (kSunriseChoices[i] == value) return kSunriseChoices[(i + 1) % sizeof(kSunriseChoices)];
  }
  return kSunriseChoices[0];
}

Action handleTouch() {
  if (M5.Touch.getCount() == 0) return Action::None;
  auto t = M5.Touch.getDetail(0);
  if (!t.wasClicked()) return Action::None;
  if (t.y < kHeaderHeight) return t.x < 110 ? Action::Back : Action::None;

  lastDraw = 0;  // redraw immediately after any tap
  switch ((t.y - kHeaderHeight) / kRowHeight) {
    case kSunrise: {
      // Ignore taps under the "< Back" button on the left so a slightly low tap on
      // "< Back" cannot accidentally cycle the sunrise duration.
      if (t.x < 110) break;
      alarmclock::Config cfg = alarmclock::config();
      cfg.sunriseMinutes = nextSunrise(cfg.sunriseMinutes);
      alarmclock::setConfig(cfg);
      break;
    }
    case kSync: timesync::start(); break;
    case kWps:  return Action::OpenWps;
    case kWifi: return Action::OpenWifi;
    case kLight: return Action::OpenHue;
    default:    break;
  }
  return Action::None;
}

void drawRow(M5Canvas& c, int row, const char* label, const String& value, uint16_t valueColor) {
  const int w = c.width();
  int y = kHeaderHeight + row * kRowHeight;
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(kText);
  c.drawString(label, 12, y + kRowHeight / 2);
  c.setTextDatum(middle_right);
  c.setTextColor(valueColor);
  c.drawString(value, w - 12, y + kRowHeight / 2);
  c.drawFastHLine(0, y + kRowHeight, w, kLine);
}

void draw(M5Canvas& c) {
  const int w = c.width();
  c.fillScreen(TFT_BLACK);
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(kText);
  c.drawString("< Back", 12, kHeaderHeight / 2);
  c.setTextDatum(middle_center);
  c.setTextColor(TFT_DARKGREY);
  c.drawString("Settings", w / 2, kHeaderHeight / 2);
  c.drawFastHLine(0, kHeaderHeight, w, kLine);

  uint8_t sunrise = alarmclock::config().sunriseMinutes;
  drawRow(c, kSunrise, "Sunrise before alarm",
          sunrise ? String(sunrise) + " min" : String("off"), kAccent);
  const String& group = hue::selectedGroupName();
  drawRow(c, kLight, "Wake-up light", group.isEmpty() ? String(HUEALARM_LAMP_MATCH) : group, kAccent);
  drawRow(c, kSync, "Sync time now", timesync::stateName(timesync::state()), TFT_DARKGREY);
  drawRow(c, kWps, "Connect Wi-Fi (WPS)", wifilink::networkName(), TFT_DARKGREY);
  drawRow(c, kWifi, "Wi-Fi test", ">", TFT_DARKGREY);
  drawRow(c, kInfo,
          HUEALARM_WAKE_MODE == HUEALARM_WAKE_BUTTON ? "Wake: side button" : "Wake: touch",
          String(M5.Power.getBatteryVoltage()) + " mV", TFT_DARKGREY);
}

}  // namespace

void enter() {
  hue::loadCredentials();  // for the wake-up light name
  lastDraw = 0;
}

Action update(M5Canvas& canvas) {
  Action action = handleTouch();
  if (action != Action::None) return action;
  if (lastDraw == 0 || millis() - lastDraw >= 200) {
    lastDraw = millis();
    draw(canvas);
    canvas.pushSprite(0, 0);
  }
  return Action::None;
}

}  // namespace settingsscreen
