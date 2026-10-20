#pragma once

#include <M5Unified.h>

// Wake-up light: connects to the Hue bridge (pairing via its link button if
// needed), lists the rooms and zones, and makes the tapped one the wake-up light.
// Its lamps are then shown (which support the sunrise) with a 1 minute test.
namespace huescreen {

void enter();
void exit();

// Handle touch, advance the connection steps and draw. Returns false when the
// user leaves the screen ("back" from the room list).
bool update(M5Canvas& canvas);

}  // namespace huescreen
