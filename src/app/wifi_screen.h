#pragma once

#include <M5Unified.h>

// Wi-Fi test screen: change radio settings, connect and scan, and see signal
// strength, connect time and failure reasons - for finding settings that work
// at the alarm clock's location.
namespace wifiscreen {

void enter();
void exit();

// Handle touch and draw. Returns false when the user tapped "back".
bool update(M5Canvas& canvas);

}  // namespace wifiscreen
