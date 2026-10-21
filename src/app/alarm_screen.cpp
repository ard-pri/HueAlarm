#include "alarm_screen.h"

#include "alarm_clock.h"
#include "wheel_picker.h"

namespace alarmscreen {
namespace {

constexpr int kHeaderHeight = 40;
constexpr int kTop = kHeaderHeight + 1;  // wheel area
const uint16_t kAccent = lgfx::color565(64, 190, 84);  // Braun bell green
const uint16_t kText = lgfx::color565(184, 180, 144);  // warm backlit LCD tone
const uint16_t kBand = lgfx::color565(36, 36, 36);

WheelPicker hourWheel(24, 40, 110);
WheelPicker minuteWheel(12, 170, 110, 5);  // 5 minute steps

// Each wheel is drawn into its own small sprite in internal RAM and only the
// wheel that moves is sent to the display - far less to transfer per frame than
// the full-screen canvas in PSRAM.
M5Canvas hourCanvas(&M5.Display);
M5Canvas minuteCanvas(&M5.Display);
bool fullRedraw = true;
int shownMinute = -1;  // the header shows the current time; redraw when it changes

String currentTime() {
  time_t now = time(nullptr);
  tm local;
  localtime_r(&now, &local);
  shownMinute = local.tm_min;
  char text[8];
  snprintf(text, sizeof(text), "%d:%02d", local.tm_hour, local.tm_min);
  return text;
}

int minuteNow() {
  time_t now = time(nullptr);
  tm local;
  localtime_r(&now, &local);
  return local.tm_min;
}
bool hourWasMoving = false;
bool minuteWasMoving = false;

// The selected row sits near the top (one row visible above it), so a finger
// dragging the wheel further down does not cover it.
constexpr int kSelectionOffset = 60;  // centre of the selected row below kTop

int bandY(int) { return kTop + kSelectionOffset - 22; }

// Returns false when the screen is finished (Cancel or Done).
bool handleTouch(int w, int h) {
  if (M5.Touch.getCount() == 0) return true;
  auto t = M5.Touch.getDetail(0);

  hourWheel.handleTouch(t, kTop, h - kTop);
  minuteWheel.handleTouch(t, kTop, h - kTop);
  if (!t.wasClicked() || t.y >= kHeaderHeight) return true;

  if (t.x < 110) return false;  // cancel
  if (t.x > w - 110) {
    alarmclock::Config cfg = alarmclock::config();
    cfg.hour = hourWheel.value();
    cfg.minute = minuteWheel.value();
    cfg.enabled = true;
    alarmclock::setConfig(cfg);
    return false;
  }
  return true;
}

// Static parts: header, selection band and colon.
void drawFrame(M5Canvas& canvas) {
  const int w = canvas.width();
  const int h = canvas.height();
  canvas.fillScreen(TFT_BLACK);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(kText);
  canvas.drawString("Cancel", 12, kHeaderHeight / 2);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString(currentTime(), w / 2, kHeaderHeight / 2);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(kAccent);
  canvas.drawString("Done", w - 12, kHeaderHeight / 2);
  canvas.drawFastHLine(0, kHeaderHeight, w, lgfx::color565(40, 40, 40));

  int height = h - kTop;
  canvas.fillRoundRect(16, bandY(height), w - 32, 44, 10, kBand);
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(kText);
  canvas.drawString(":", w / 2, bandY(height) + 22 - 3);
}

void drawWheel(M5Canvas& column, const WheelPicker& wheel, int height) {
  column.fillScreen(TFT_BLACK);
  column.fillRect(0, bandY(height) - kTop, column.width(), 44, kBand);  // band behind the centre row
  wheel.draw(column, kTop, height, wheel.x(), kTop);
  column.pushSprite(wheel.x(), kTop);
}

void ensureColumn(M5Canvas& column, const WheelPicker& wheel, int height) {
  if (column.width() > 0) return;
  column.setPsram(false);
  column.setColorDepth(16);
  column.createSprite(wheel.width(), height);
}

}  // namespace

void enter() {
  hourWheel.setSelectionOffset(kSelectionOffset);
  minuteWheel.setSelectionOffset(kSelectionOffset);
  const auto& cfg = alarmclock::config();
  hourWheel.setValue(cfg.hour);
  minuteWheel.setValue(cfg.minute);
  fullRedraw = true;
}

bool update(M5Canvas& canvas) {
  const int w = canvas.width();
  const int h = canvas.height();
  const int height = h - kTop;
  if (!handleTouch(w, h)) return false;

  bool hourMoving = hourWheel.animate();
  bool minuteMoving = minuteWheel.animate();

  if (minuteNow() != shownMinute) fullRedraw = true;
  if (fullRedraw) {
    ensureColumn(hourCanvas, hourWheel, height);
    ensureColumn(minuteCanvas, minuteWheel, height);
    drawFrame(canvas);
    canvas.pushSprite(0, 0);
    drawWheel(hourCanvas, hourWheel, height);
    drawWheel(minuteCanvas, minuteWheel, height);
    fullRedraw = false;
  } else {
    // Redraw a wheel while it moves, plus once more for its final position.
    if (hourMoving || hourWasMoving) drawWheel(hourCanvas, hourWheel, height);
    if (minuteMoving || minuteWasMoving) drawWheel(minuteCanvas, minuteWheel, height);
  }
  hourWasMoving = hourMoving;
  minuteWasMoving = minuteMoving;
  return true;
}

}  // namespace alarmscreen
