#pragma once

#include <M5Unified.h>

// Wi-Fi setup with WPS push button: the clock waits up to 2 minutes for the WPS
// button on the router, connects with the credentials it receives and stores
// them (wifilink::setCredentials).
namespace wpsscreen {

void enter();
void exit();
bool active();  // WPS is waiting / connecting (keep the screen on)

// Handle touch and draw. Returns false when the user tapped "back".
bool update(M5Canvas& canvas);

}  // namespace wpsscreen
