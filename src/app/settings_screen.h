#pragma once

#include <M5Unified.h>

// Settings, reached by a long press on the clock: sunrise duration, time sync,
// Wi-Fi setup (WPS) and the Wi-Fi / Hue test screens.
namespace settingsscreen {

enum class Action { None, Back, OpenWps, OpenWifi, OpenHue };

void enter();
Action update(M5Canvas& canvas);

}  // namespace settingsscreen
