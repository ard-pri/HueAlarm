#include "clock_face.h"

#include "alarm_clock.h"

namespace clockface {
namespace {

// Colours of the backlit Braun LCD (warm mid-brightness LCD tone, not white).
constexpr uint16_t kBackground = 0x0000;
const uint16_t kSegment = lgfx::color565(184, 180, 144);    // warm backlit LCD digits
const uint16_t kBellGreen = lgfx::color565(64, 190, 84);
const uint16_t kBellOff = lgfx::color565(46, 46, 42);       // faint outline when off

constexpr int kRight = 306;          // right edge of both times
constexpr int kTimeY = 66;           // centre of the large time
constexpr float kTimeSize = 1.85f;   // Font7 is 48 px high -> ~89 px
constexpr int kAlarmY = 190;         // centre of the alarm row
constexpr float kAlarmSize = 1.15f;  // ~55 px
constexpr int kBellX = 48;
constexpr int kBellSize = 46;
constexpr int kRowTop = 140;         // top of the alarm row (touch zones)
constexpr int kBellZoneRight = 120;
constexpr int kLowBatteryMv = 3600;

// Hour without a leading zero, like the original ("7:00").
void formatTime(char* text, size_t size, int hour, int minute) {
  snprintf(text, size, "%d:%02d", hour, minute);
}

void drawSegments(M5Canvas& c, const char* text, int y, float size, uint16_t color) {
  c.setFont(&fonts::Font7);
  c.setTextSize(size);
  c.setTextDatum(middle_right);
  c.setTextColor(color);
  c.drawString(text, kRight, y);
  c.setTextSize(1);
}

// Bell like the Braun symbol: rounded dome, flared rim, clapper.
void drawBell(M5Canvas& c, int cx, int cy, int s, uint16_t color, bool filled) {
  int domeW = s * 3 / 4, domeH = s * 3 / 4;
  int domeX = cx - domeW / 2, domeY = cy - s / 2;
  int rimW = s, rimH = max(4, s / 6);
  int rimY = domeY + domeH - rimH / 2;
  int clapperR = max(3, s / 9);
  if (filled) {
    c.fillRoundRect(domeX, domeY, domeW, domeH, domeW / 2, color);
    c.fillRect(domeX, domeY + domeH / 2, domeW, domeH / 2, color);
    c.fillRoundRect(cx - rimW / 2, rimY, rimW, rimH, rimH / 2, color);
    c.fillCircle(cx, rimY + rimH + clapperR, clapperR, color);
  } else {
    c.drawRoundRect(domeX, domeY, domeW, domeH + rimH, domeW / 2, color);
    c.drawRoundRect(cx - rimW / 2, rimY, rimW, rimH, rimH / 2, color);
    c.drawCircle(cx, rimY + rimH + clapperR, clapperR, color);
  }
}

void drawBattery(M5Canvas& c, uint16_t color, int fillWidth) {
  constexpr int x = 12, y = 10, w = 24, h = 12;
  c.drawRect(x, y, w, h, color);
  c.fillRect(x + w, y + 3, 2, h - 6, color);  // terminal
  if (fillWidth > 0) c.fillRect(x + 2, y + 2, fillWidth, h - 4, color);
}

// Top left, only when there is something to say:
//   on USB, charging  -> blinking battery
//   on USB, full      -> green battery
//   below ~3.6 V      -> nearly empty battery (~15-20 %, about a week left in touch
//                        mode; the power chip's percentage is unreliable, the voltage is not)
void drawBatteryStatus(M5Canvas& c) {
  bool onUsb = M5.Power.getVBUSVoltage() > 4000;
  if (onUsb) {
    // The power chip's charge status. (The INA3221 "battery current" is no use here:
    // it showed a constant ~168 mA on USB with the battery full at 4.16 V.)
    if (M5.Power.isCharging() == m5::Power_Class::is_charging) {
      if ((millis() / 600) % 2) drawBattery(c, kSegment, 10);
    } else {
      drawBattery(c, kBellGreen, 20);
    }
    return;
  }
  int mv = M5.Power.getBatteryVoltage();
  if (mv > 0 && mv < kLowBatteryMv) drawBattery(c, kSegment, 3);
}

// Small sun: the alarm also starts the lamp's sunrise.
void drawSun(M5Canvas& c, int cx, int cy, uint16_t color) {
  c.fillCircle(cx, cy, 5, color);
  for (int i = 0; i < 8; ++i) {
    float a = i * PI / 4;
    c.drawLine(cx + cosf(a) * 8, cy + sinf(a) * 8, cx + cosf(a) * 11, cy + sinf(a) * 11, color);
  }
}

void drawTimeOfDay(M5Canvas& c, time_t now, uint16_t color) {
  char text[8] = "-:--";
  if (now > 1700000000) {
    tm local;
    localtime_r(&now, &local);
    formatTime(text, sizeof(text), local.tm_hour, local.tm_min);
  }
  drawSegments(c, text, kTimeY, kTimeSize, color);
}

void drawAlarmTime(M5Canvas& c, uint16_t color) {
  const auto& cfg = alarmclock::config();
  char text[8];
  formatTime(text, sizeof(text), cfg.hour, cfg.minute);
  drawSegments(c, text, kAlarmY, kAlarmSize, color);
}

}  // namespace

bool isBell(int x, int y) { return y >= kRowTop && x < kBellZoneRight; }

bool isAlarmTime(int x, int y) { return y >= kRowTop && x >= kBellZoneRight; }

void draw(M5Canvas& c, time_t now) {
  c.fillScreen(kBackground);
  drawTimeOfDay(c, now, kSegment);

  const auto& cfg = alarmclock::config();
  bool on = cfg.enabled;
  drawBell(c, kBellX, kAlarmY, kBellSize, on ? kBellGreen : kBellOff, on);
  if (cfg.light) drawSun(c, kBellX + kBellSize / 2 + 16, kAlarmY - kBellSize / 2 + 4,
                         on ? kBellGreen : kBellOff);
  drawAlarmTime(c, kSegment);

  if (alarmclock::state() == alarmclock::State::Snoozed) {
    c.setFont(&fonts::FreeSansBold9pt7b);
    c.setTextDatum(bottom_left);
    c.setTextColor(kBellGreen);
    c.drawString("z", kBellX + kBellSize / 2, kAlarmY - kBellSize / 2 + 4);
    c.drawString("Z", kBellX + kBellSize / 2 + 10, kAlarmY - kBellSize / 2 - 6);
  }

  drawBatteryStatus(c);
}

void drawRinging(M5Canvas& c, time_t now) {
  c.fillScreen(kBackground);
  bool phase = (millis() / 400) % 2;
  drawTimeOfDay(c, now, phase ? kBellGreen : kSegment);
  drawBell(c, kBellX + (phase ? -4 : 4), kAlarmY, kBellSize + 6, kBellGreen, true);
  drawAlarmTime(c, kSegment);
}

}  // namespace clockface
