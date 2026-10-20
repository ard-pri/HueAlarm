#include "hue.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include <set>

#include "log.h"

namespace hue {
namespace {

constexpr const char* kPrefsNamespace = "hue";
constexpr const char* kDeviceType = "huealarm#m5stack";
constexpr uint16_t kTimeoutMs = 8000;
constexpr int kAttempts = 3;
constexpr int kJsonError = -100;  // 2xx response whose body could not be parsed completely

String ip;
String appKey;
String lightCache;  // comma-separated light ids of the wake-up lights
String groupId;     // selected room / zone ("" = default lamp match)
String groupName;
bool groupZone = false;
String lastErrorText;

void saveCredentials() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString("ip", ip);
  prefs.putString("key", appKey);
  prefs.putString("lights", lightCache);
  prefs.putString("group", groupId);
  prefs.putString("gname", groupName);
  prefs.putBool("gzone", groupZone);
  prefs.end();
}

bool containsIgnoreCase(String haystack, String needle) {
  haystack.toLowerCase();
  needle.toLowerCase();
  return haystack.indexOf(needle) >= 0;
}

// One HTTPS request to the bridge. The bridge uses a self-signed certificate, so
// the certificate is not verified (local network only).
// Returns the HTTP status code, a negative HTTPClient error, or kJsonError.
int requestOnce(const char* method, const String& path, const String& body, JsonDocument& out,
                const JsonDocument* filter) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  http.setConnectTimeout(kTimeoutMs);
  http.useHTTP10(true);  // no chunked encoding, so the body can be parsed as a stream
  if (!http.begin(client, "https://" + ip + path)) return HTTPC_ERROR_CONNECTION_REFUSED;
  if (!appKey.isEmpty()) http.addHeader("hue-application-key", appKey);
  if (!body.isEmpty()) http.addHeader("Content-Type", "application/json");

  uint32_t start = millis();
  out.clear();
  int code = http.sendRequest(method, body);
  if (code > 0) {
    DeserializationError err = filter
        ? deserializeJson(out, http.getStream(), DeserializationOption::Filter(*filter))
        : deserializeJson(out, http.getStream());
    if (err && code >= 200 && code < 300) {
      lastErrorText = String(method) + " " + path + ": JSON " + err.c_str();
      code = kJsonError;
    } else if (code >= 300) {
      lastErrorText = String(method) + " " + path + ": HTTP " + code;
    }
  } else {
    lastErrorText = String(method) + " " + path + ": " + HTTPClient::errorToString(code);
  }
  Log.printf("[hue] %s %s -> %d (%lu ms)\n", method, path.c_str(), code,
             (unsigned long)(millis() - start));
  http.end();
  return code;
}

// Request with retries on connection errors, truncated bodies and server errors.
int request(const char* method, const String& path, const String& body, JsonDocument& out,
            const JsonDocument* filter = nullptr) {
  int code = 0;
  for (int attempt = 1; attempt <= kAttempts; ++attempt) {
    code = requestOnce(method, path, body, out, filter);
    bool transient = code < 0 || code >= 500;
    if (!transient) break;
    if (attempt < kAttempts) {
      Log.printf("[hue] retrying (%s)\n", lastErrorText.c_str());
      delay(300 * attempt);
    }
  }
  return code;
}

Result resultFromCode(int code) {
  if (code >= 200 && code < 300) return Result::Ok;
  if (code == 401 || code == 403) return Result::Unauthorized;
  return Result::Error;
}

bool discoverMdns() {
  if (!MDNS.begin("huealarm")) return false;
  int count = MDNS.queryService("hue", "tcp");
  if (count > 0) ip = MDNS.IP(0).toString();
  MDNS.end();
  Log.printf("[hue] mDNS: %d bridge(s)%s%s\n", count, count > 0 ? ", using " : "",
             count > 0 ? ip.c_str() : "");
  return count > 0;
}

bool discoverCloud() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(client, "https://discovery.meethue.com/")) return false;
  int code = http.GET();
  JsonDocument doc;
  bool found = false;
  if (code == 200 && !deserializeJson(doc, http.getStream())) {
    const char* addr = doc[0]["internalipaddress"];
    if (addr) {
      ip = addr;
      found = true;
    }
  }
  http.end();
  Log.printf("[hue] cloud discovery -> %d%s%s\n", code, found ? ", using " : "",
             found ? ip.c_str() : "");
  if (!found) lastErrorText = String("discovery: mDNS none, cloud ") + code;
  return found;
}

// Filter entries for the light fields we use.
void addLightFields(JsonObject item) {
  item["id"] = true;
  item["metadata"]["name"] = true;
  item["on"]["on"] = true;
  item["dimming"]["brightness"] = true;
  item["timed_effects"]["effect_values"] = true;
}

Light parseLight(JsonObject item) {
  Light light;
  light.id = item["id"] | "";
  light.name = item["metadata"]["name"] | "";
  light.on = item["on"]["on"] | false;
  light.brightness = item["dimming"]["brightness"] | 0.0f;
  for (const char* effect : item["timed_effects"]["effect_values"].as<JsonArray>()) {
    if (effect && strcmp(effect, "sunrise") == 0) light.sunrise = true;
  }
  return light;
}

void logLight(const Light& light) {
  Log.printf("[hue] light '%s' %s %.0f%% id %s\n", light.name.c_str(), light.on ? "on" : "off",
             light.brightness, light.id.c_str());
}

// Fast path: load the cached light ids one by one (small responses).
Result loadCachedLights(std::vector<Light>& out) {
  JsonDocument filter;
  addLightFields(filter["data"][0].to<JsonObject>());
  int start = 0;
  while (start < (int)lightCache.length()) {
    int end = lightCache.indexOf(',', start);
    if (end < 0) end = lightCache.length();
    String id = lightCache.substring(start, end);
    start = end + 1;

    JsonDocument doc;
    int code = request("GET", "/clip/v2/resource/light/" + id, "", doc, &filter);
    if (code == 404) {
      Log.println("[hue] cached light id is gone, doing a full lookup");
      lightCache = "";
      saveCredentials();
      out.clear();
      return Result::Error;
    }
    Result result = resultFromCode(code);
    if (result != Result::Ok) return result;
    if (doc["data"].size() == 0) {
      lastErrorText = "light " + id + ": empty response";
      return Result::Error;
    }
    out.push_back(parseLight(doc["data"][0]));
    logLight(out.back());
  }
  return Result::Ok;
}

// Slow path: all devices and all lights, matched by product / name.
Result lookupLights(const char* match, std::vector<Light>& out) {
  JsonDocument deviceFilter;
  deviceFilter["data"][0]["metadata"]["name"] = true;
  deviceFilter["data"][0]["product_data"]["product_name"] = true;
  deviceFilter["data"][0]["services"][0]["rid"] = true;
  deviceFilter["data"][0]["services"][0]["rtype"] = true;
  JsonDocument devices;
  int code = request("GET", "/clip/v2/resource/device", "", devices, &deviceFilter);
  if (resultFromCode(code) != Result::Ok) return resultFromCode(code);

  std::set<String> lightIds;
  for (JsonObject device : devices["data"].as<JsonArray>()) {
    String product = device["product_data"]["product_name"] | "";
    String name = device["metadata"]["name"] | "";
    if (!containsIgnoreCase(product, match) && !containsIgnoreCase(name, match)) continue;
    Log.printf("[hue] matched device '%s' (%s)\n", name.c_str(), product.c_str());
    for (JsonObject service : device["services"].as<JsonArray>()) {
      if (strcmp(service["rtype"] | "", "light") == 0) lightIds.insert(service["rid"] | "");
    }
  }

  JsonDocument lightFilter;
  addLightFields(lightFilter["data"][0].to<JsonObject>());
  JsonDocument lights;
  code = request("GET", "/clip/v2/resource/light", "", lights, &lightFilter);
  if (resultFromCode(code) != Result::Ok) return resultFromCode(code);

  for (JsonObject item : lights["data"].as<JsonArray>()) {
    Light light = parseLight(item);
    if (!lightIds.count(light.id) && !containsIgnoreCase(light.name, match)) continue;
    logLight(light);
    out.push_back(light);
  }
  Log.printf("[hue] %u devices, %u lights checked\n", devices["data"].size(), lights["data"].size());
  return Result::Ok;
}

// All lights of a room (its children are devices) or zone (its children are lights).
Result lookupGroupLights(const String& id, bool zone, std::vector<Light>& out) {
  JsonDocument groupFilter;
  groupFilter["data"][0]["children"][0]["rid"] = true;
  groupFilter["data"][0]["children"][0]["rtype"] = true;
  JsonDocument group;
  int code = request("GET", String("/clip/v2/resource/") + (zone ? "zone/" : "room/") + id, "",
                     group, &groupFilter);
  if (resultFromCode(code) != Result::Ok) return resultFromCode(code);

  std::set<String> lightIds, deviceIds;
  for (JsonObject child : group["data"][0]["children"].as<JsonArray>()) {
    String rtype = child["rtype"] | "";
    if (rtype == "light") lightIds.insert(child["rid"] | "");
    if (rtype == "device") deviceIds.insert(child["rid"] | "");
  }

  if (!deviceIds.empty()) {
    JsonDocument deviceFilter;
    deviceFilter["data"][0]["id"] = true;
    deviceFilter["data"][0]["services"][0]["rid"] = true;
    deviceFilter["data"][0]["services"][0]["rtype"] = true;
    JsonDocument devices;
    code = request("GET", "/clip/v2/resource/device", "", devices, &deviceFilter);
    if (resultFromCode(code) != Result::Ok) return resultFromCode(code);
    for (JsonObject device : devices["data"].as<JsonArray>()) {
      if (!deviceIds.count(device["id"] | "")) continue;
      for (JsonObject service : device["services"].as<JsonArray>()) {
        if (strcmp(service["rtype"] | "", "light") == 0) lightIds.insert(service["rid"] | "");
      }
    }
  }

  JsonDocument lightFilter;
  addLightFields(lightFilter["data"][0].to<JsonObject>());
  JsonDocument lights;
  code = request("GET", "/clip/v2/resource/light", "", lights, &lightFilter);
  if (resultFromCode(code) != Result::Ok) return resultFromCode(code);
  for (JsonObject item : lights["data"].as<JsonArray>()) {
    Light light = parseLight(item);
    if (!lightIds.count(light.id)) continue;
    logLight(light);
    out.push_back(light);
  }
  return Result::Ok;
}

// Cache the ids of the lights that take part (those with sunrise; all if none has it).
void cacheLights(const std::vector<Light>& lights) {
  bool anySunrise = false;
  for (const auto& light : lights) anySunrise |= light.sunrise;
  lightCache = "";
  for (const auto& light : lights) {
    if (anySunrise && !light.sunrise) continue;
    if (!lightCache.isEmpty()) lightCache += ',';
    lightCache += light.id;
  }
  saveCredentials();
  Log.printf("[hue] wake-up light ids: %s\n", lightCache.c_str());
}

}  // namespace

void loadCredentials() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  ip = prefs.getString("ip", "");
  appKey = prefs.getString("key", "");
  lightCache = prefs.getString("lights", "");
  groupId = prefs.getString("group", "");
  groupName = prefs.getString("gname", "");
  groupZone = prefs.getBool("gzone", false);
  prefs.end();
  Log.printf("[hue] stored bridge: %s, app key: %s, lamp ids: %s\n",
             ip.isEmpty() ? "none" : ip.c_str(), appKey.isEmpty() ? "none" : "yes",
             lightCache.isEmpty() ? "none" : lightCache.c_str());
}

bool hasBridge() { return !ip.isEmpty(); }

bool hasAppKey() { return !appKey.isEmpty(); }

const String& bridgeIp() { return ip; }

const String& lastError() { return lastErrorText; }

bool discover() {
  bool found = discoverMdns() || discoverCloud();
  if (found) saveCredentials();
  return found;
}

Result pair() {
  String savedKey = appKey;
  appKey = "";  // the pairing request must not carry a key
  JsonDocument doc;
  String body = String("{\"devicetype\":\"") + kDeviceType + "\",\"generateclientkey\":true}";
  int code = request("POST", "/api", body, doc);
  appKey = savedKey;
  if (code != 200) return Result::Error;

  if (doc[0]["success"]["username"].is<const char*>()) {
    appKey = doc[0]["success"]["username"].as<const char*>();
    saveCredentials();
    Log.println("[hue] paired, application key stored");
    return Result::Ok;
  }
  int errorType = doc[0]["error"]["type"] | 0;
  if (errorType == 101) return Result::LinkButtonNotPressed;
  lastErrorText = String("pairing error ") + errorType;
  Log.printf("[hue] pairing error %d: %s\n", errorType,
             doc[0]["error"]["description"] | "unknown");
  return Result::Error;
}

void forgetAppKey() {
  appKey = "";
  saveCredentials();
}

Result findLights(const char* match, std::vector<Light>& out) {
  out.clear();
  if (!lightCache.isEmpty()) {
    Result result = loadCachedLights(out);
    if (result == Result::Ok || lightCache.length() > 0) return result;
    // The cache was invalidated: fall through to a full lookup.
  }

  Result result = groupId.isEmpty() ? lookupLights(match, out)
                                    : lookupGroupLights(groupId, groupZone, out);
  if (result == Result::Ok && !out.empty()) {
    cacheLights(out);
    // Only the lights that take part.
    std::vector<Light> used;
    for (const auto& light : out) {
      if (lightCache.indexOf(light.id) >= 0) used.push_back(light);
    }
    out = used;
  }
  return result;
}

Result listGroups(std::vector<Group>& out) {
  out.clear();
  JsonDocument filter;
  filter["data"][0]["id"] = true;
  filter["data"][0]["metadata"]["name"] = true;
  for (bool zone : {false, true}) {
    JsonDocument doc;
    int code = request("GET", zone ? "/clip/v2/resource/zone" : "/clip/v2/resource/room", "", doc,
                       &filter);
    if (resultFromCode(code) != Result::Ok) return resultFromCode(code);
    for (JsonObject item : doc["data"].as<JsonArray>()) {
      Group group;
      group.id = item["id"] | "";
      group.name = item["metadata"]["name"] | "";
      group.zone = zone;
      out.push_back(group);
    }
  }
  return Result::Ok;
}

Result selectGroup(const Group& group, std::vector<Light>& out) {
  out.clear();
  Result result = lookupGroupLights(group.id, group.zone, out);
  if (result != Result::Ok) return result;
  groupId = group.id;
  groupName = group.name;
  groupZone = group.zone;
  cacheLights(out);
  Log.printf("[hue] wake-up light: %s %s\n", group.zone ? "zone" : "room", group.name.c_str());
  return Result::Ok;
}

const String& selectedGroupName() { return groupName; }

Result setOn(const String& lightId, bool on) {
  JsonDocument doc;
  int code = request("PUT", "/clip/v2/resource/light/" + lightId,
                     on ? "{\"on\":{\"on\":true}}" : "{\"on\":{\"on\":false}}", doc);
  return resultFromCode(code);
}

Result logEffects(const String& lightId) {
  JsonDocument filter;
  filter["data"][0]["metadata"]["name"] = true;
  filter["data"][0]["timed_effects"] = true;
  filter["data"][0]["effects"]["effect_values"] = true;
  filter["data"][0]["effects_v2"]["action"]["effect_values"] = true;
  JsonDocument doc;
  int code = request("GET", "/clip/v2/resource/light/" + lightId, "", doc, &filter);
  if (resultFromCode(code) == Result::Ok) {
    String json;
    serializeJson(doc["data"][0], json);
    Log.printf("[hue] effects of %s: %s\n", lightId.c_str(), json.c_str());
  }
  return resultFromCode(code);
}

Result startSunrise(const String& lightId, uint32_t durationMs, bool switchOn) {
  JsonDocument doc;
  String body = String("{") + (switchOn ? "\"on\":{\"on\":true}," : "") +
                "\"timed_effects\":{\"effect\":\"sunrise\",\"duration\":" + durationMs + "}}";
  int code = request("PUT", "/clip/v2/resource/light/" + lightId, body, doc);
  if (code >= 300) {
    String errors;
    serializeJson(doc["errors"], errors);
    Log.printf("[hue] sunrise rejected: %s\n", errors.c_str());
  }
  return resultFromCode(code);
}

Result stopTimedEffect(const String& lightId) {
  JsonDocument doc;
  int code = request("PUT", "/clip/v2/resource/light/" + lightId,
                     "{\"timed_effects\":{\"effect\":\"no_effect\"}}", doc);
  return resultFromCode(code);
}

}  // namespace hue
