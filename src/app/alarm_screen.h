#pragma once

#include <M5Unified.h>

// Alarm time setting: hours | minutes scroll wheels, "Cancel" / "Done".
// "Done" stores the time and switches the alarm on.
namespace alarmscreen {

void enter();

// Handle touch and draw. Returns false when the screen is finished.
bool update(M5Canvas& canvas);

}  // namespace alarmscreen
