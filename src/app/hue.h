#pragma once

#include <Arduino.h>

#include <vector>

// Philips Hue bridge access (CLIP API v2 over HTTPS). All calls block and need Wi-Fi.
namespace hue {

struct Light {
  String id;        // light resource id (v2)
  String name;      // light name as set in the Hue app
  bool on = false;
  float brightness = 0;  // 0..100 %, 0 if the light is not dimmable
  bool sunrise = false;  // supports the sunrise effect (not all Hue lamps do)
};

// A room or zone from the Hue app; the wake-up light is chosen as one of these.
struct Group {
  String id;
  String name;
  bool zone = false;
};

enum class Result { Ok, LinkButtonNotPressed, Unauthorized, Error };

// Load bridge address and application key from flash.
void loadCredentials();
bool hasBridge();
bool hasAppKey();
const String& bridgeIp();

// Technical description of the last failed request, for diagnostics.
const String& lastError();

// Find the bridge on the local network (mDNS, then the Hue discovery service).
bool discover();

// One pairing attempt. The user must have pressed the bridge link button
// within the last 30 seconds. Stores the application key on success.
Result pair();

// Forget the application key (e.g. after it was revoked in the Hue app).
void forgetAppKey();

// The wake-up lights: those of the selected room / zone that support the sunrise;
// without a selection, lights of a device whose product name or name contains
// `match` (case-insensitive). The light ids are cached in flash; later calls only
// load those lights.
Result findLights(const char* match, std::vector<Light>& out);

// Rooms and zones of the bridge.
Result listGroups(std::vector<Group>& out);

// Make `group` the wake-up light. `out` gets all its lights (check `sunrise` to
// see which of them take part).
Result selectGroup(const Group& group, std::vector<Light>& out);

// Name of the selected room / zone ("" = none selected, default lamp match).
const String& selectedGroupName();

Result setOn(const String& lightId, bool on);

// Log which timed effects (sunrise, sunset) and effects the light supports.
Result logEffects(const String& lightId);

// Start the light's built-in sunrise: it fades up over `durationMs` (max 6 h).
// The bridge and the lamp run the animation on their own. The Twilight ignores
// the sunrise while it is off; `switchOn` sends "on" in the same request, which
// starts it from off without a visible flash.
Result startSunrise(const String& lightId, uint32_t durationMs, bool switchOn = false);

// Stop a running sunrise / sunset.
Result stopTimedEffect(const String& lightId);

}  // namespace hue
