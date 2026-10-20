#pragma once

#include <M5Unified.h>
#include <time.h>

// Clock face after the Braun BC08B (black, backlit inverted LCD): large beige
// 7-segment time at the top, the green alarm bell bottom left, the alarm time
// bottom right. A small sun next to the bell: the lamp's sunrise wakes too (without
// it: beep only). No AM/PM.
namespace clockface {

// Touch zones of the clock face.
bool isBell(int x, int y);       // bottom left: alarm on / off (long press: light on / off)
bool isAlarmTime(int x, int y);  // bottom right: set the alarm time

void draw(M5Canvas& canvas, time_t now);

// Ringing: the time flashes and the bell swings; tapping anywhere snoozes, the
// bell stops the alarm.
void drawRinging(M5Canvas& canvas, time_t now);

}  // namespace clockface
