#include "hue_screen.h"

#include <vector>

#include "config.h"
#include "hue.h"
#include "log.h"
#include "wifi_link.h"

namespace huescreen {
namespace {

constexpr uint32_t kPairWindowMs = 60000;
constexpr uint32_t kPairIntervalMs = 2000;
constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 33;
constexpr int kBarHeight = 40;
constexpr uint32_t kTestSunriseMs = 60000;
const uint16_t kAccent = lgfx::color565(64, 190, 84);  // Braun bell green
const uint16_t kText = lgfx::color565(204, 202, 172);
const uint16_t kLine = lgfx::color565(40, 40, 40);

// WaitWifi -> (Discover) -> (Pair) -> LoadGroups -> Groups -> Selecting -> Lights
enum class Step { WaitWifi, Discover, Pair, LoadGroups, Groups, Selecting, Lights, Error };

Step step = Step::WaitWifi;
bool stepDrawn = false;  // blocking work only runs after its status was shown once
uint32_t stepSince = 0;
uint32_t lastPairAttempt = 0;
bool rediscovered = false;
String errorText;
String errorDetail;
std::vector<hue::Group> groups;
std::vector<hue::Light> lights;
int selected = -1;     // index into groups being selected
int scrollOffset = 0;  // pixels, for long room lists
bool testPending = false;  // runs after "Sending..." was drawn
String testText;

void go(Step next) {
  step = next;
  stepDrawn = false;
  stepSince = millis();
}

void fail(const String& text, const String& detail = "") {
  errorText = text;
  errorDetail = detail;
  Log.printf("[hue] error: %s (%s)\n", text.c_str(), detail.c_str());
  go(Step::Error);
}

Step afterConnect() {
  return hue::hasBridge() ? (hue::hasAppKey() ? Step::LoadGroups : Step::Pair) : Step::Discover;
}

void restart() {
  rediscovered = false;
  if (wifilink::failed()) {  // retry Wi-Fi
    wifilink::release();
    wifilink::request();
  }
  go(wifilink::connected() ? afterConnect() : Step::WaitWifi);
}

// A request failed: the bridge may have a new IP address (look once) or the app
// key may have been revoked in the Hue app (pair again).
void requestFailed(hue::Result result) {
  if (result == hue::Result::Unauthorized) {
    hue::forgetAppKey();
    go(Step::Pair);
  } else if (!rediscovered) {
    rediscovered = true;
    go(Step::Discover);
  } else {
    fail("Bridge not reachable", hue::lastError());
  }
}

void advance() {
  switch (step) {
    case Step::WaitWifi:
      if (wifilink::connected()) {
        go(afterConnect());
      } else if (wifilink::failed()) {
        fail("Wi-Fi connection failed", wifilink::reasonName(wifilink::stats().lastReason));
      }
      break;

    case Step::Discover:
      if (hue::discover()) {
        go(hue::hasAppKey() ? Step::LoadGroups : Step::Pair);
      } else {
        fail("No Hue bridge found", hue::lastError());
      }
      break;

    case Step::Pair:
      if (millis() - stepSince > kPairWindowMs) {
        fail("Link button was not pressed");
      } else if (millis() - lastPairAttempt > kPairIntervalMs) {
        lastPairAttempt = millis();
        switch (hue::pair()) {
          case hue::Result::Ok:                   go(Step::LoadGroups); break;
          case hue::Result::LinkButtonNotPressed: break;  // keep waiting
          default:                                fail("Pairing failed", hue::lastError()); break;
        }
      }
      break;

    case Step::LoadGroups: {
      hue::Result result = hue::listGroups(groups);
      if (result != hue::Result::Ok) {
        requestFailed(result);
      } else if (groups.empty()) {
        fail("No rooms or zones", "Create one in the Hue app");
      } else {
        scrollOffset = 0;
        go(Step::Groups);
      }
      break;
    }

    case Step::Selecting: {
      hue::Result result = hue::selectGroup(groups[selected], lights);
      if (result != hue::Result::Ok) {
        requestFailed(result);
      } else {
        testText = "";
        go(Step::Lights);
      }
      break;
    }

    default:
      break;
  }
}

// Same request as the alarm: switch on and start the sunrise in one go.
void runSunTest() {
  bool ok = true;
  int count = 0;
  for (auto& light : lights) {
    if (!light.sunrise) continue;
    ok &= hue::startSunrise(light.id, kTestSunriseMs, true) == hue::Result::Ok;
    ++count;
  }
  testText = count == 0 ? "No lamp here supports the sunrise"
             : ok       ? "Sunrise started (1 min)"
                        : "Sunrise FAILED";
  Log.printf("[hue] test: %s\n", testText.c_str());
}

int visibleRows() { return (M5.Display.height() - kHeaderHeight) / kRowHeight; }

// Returns false when "back" leaves the screen.
bool handleTouch() {
  if (M5.Touch.getCount() == 0) return true;
  auto t = M5.Touch.getDetail(0);

  // Drag the room list when it is longer than the screen.
  if (step == Step::Groups && t.isDragging()) {
    int maxOffset = max(0, ((int)groups.size() - visibleRows()) * kRowHeight);
    scrollOffset = constrain(scrollOffset - t.deltaY(), 0, maxOffset);
    return true;
  }
  if (!t.wasClicked()) return true;

  if (t.y < kHeaderHeight && t.x < 110) {  // back
    if (step == Step::Lights) {
      go(Step::Groups);
      return true;
    }
    return false;
  }

  if (step == Step::Error) {
    restart();
  } else if (step == Step::Groups && t.y >= kHeaderHeight) {
    int index = (t.y - kHeaderHeight + scrollOffset) / kRowHeight;
    if (index < (int)groups.size()) {
      selected = index;
      go(Step::Selecting);
    }
  } else if (step == Step::Lights && t.y >= M5.Display.height() - kBarHeight) {
    testPending = true;
    testText = "Sending...";
  }
  return true;
}

void drawStatus(M5Canvas& c, const String& line1, const String& line2 = "") {
  const int w = c.width();
  c.setTextDatum(middle_center);
  c.setFont(&fonts::FreeSans12pt7b);
  c.setTextColor(kText);
  c.drawString(line1, w / 2, 110);
  if (!line2.isEmpty()) {
    c.setFont(&fonts::FreeSans9pt7b);
    c.setTextColor(TFT_LIGHTGREY);
    c.drawString(line2, w / 2, 145);
  }
}

void drawRow(M5Canvas& c, int y, const String& label, const String& value, uint16_t labelColor,
             uint16_t valueColor) {
  const int w = c.width();
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(labelColor);
  c.drawString(label, 12, y + kRowHeight / 2);
  c.setTextDatum(middle_right);
  c.setTextColor(valueColor);
  c.drawString(value, w - 12, y + kRowHeight / 2);
  c.drawFastHLine(0, y + kRowHeight, w, kLine);
}

void draw(M5Canvas& c) {
  const int w = c.width();
  const int h = c.height();
  c.fillScreen(TFT_BLACK);

  switch (step) {
    case Step::WaitWifi:
      drawStatus(c, "Connecting Wi-Fi...");
      break;
    case Step::Discover:
      drawStatus(c, "Searching Hue bridge...");
      break;
    case Step::Pair: {
      int left = (int)(kPairWindowMs - (millis() - stepSince)) / 1000;
      drawStatus(c, "Press the link button", String("on the Hue bridge (") + left + " s)");
      break;
    }
    case Step::LoadGroups:
      drawStatus(c, "Loading rooms...");
      break;
    case Step::Selecting:
      drawStatus(c, "Loading lamps...", groups[selected].name);
      break;
    case Step::Error:
      drawStatus(c, errorText, errorDetail);
      c.setTextColor(kAccent);
      c.drawString("Tap to retry", w / 2, 185);
      break;

    case Step::Groups: {
      c.setClipRect(0, kHeaderHeight + 1, w, h - kHeaderHeight - 1);
      for (size_t i = 0; i < groups.size(); ++i) {
        int y = kHeaderHeight + (int)i * kRowHeight - scrollOffset;
        if (y + kRowHeight < kHeaderHeight || y > h) continue;
        bool current = groups[i].name == hue::selectedGroupName();
        drawRow(c, y, groups[i].name, current ? "selected" : (groups[i].zone ? "zone" : "room"),
                current ? kAccent : kText, current ? kAccent : TFT_DARKGREY);
      }
      c.clearClipRect();
      break;
    }

    case Step::Lights: {
      int rows = visibleRows() - 1;  // the last row is the test button
      for (int i = 0; i < (int)lights.size() && i < rows; ++i) {
        int y = kHeaderHeight + i * kRowHeight;
        drawRow(c, y, lights[i].name, lights[i].sunrise ? "sunrise" : "no sunrise",
                lights[i].sunrise ? kText : TFT_DARKGREY,
                lights[i].sunrise ? kAccent : TFT_DARKGREY);
      }
      int barY = h - kBarHeight;
      c.fillRect(0, barY, w, kBarHeight, TFT_BLACK);
      c.drawFastHLine(0, barY, w, kLine);
      c.setFont(&fonts::FreeSans9pt7b);
      c.setTextDatum(middle_center);
      c.setTextColor(testText.isEmpty() ? kAccent : TFT_LIGHTGREY);
      c.drawString(testText.isEmpty() ? "Test sunrise (1 min)" : testText, w / 2, barY + kBarHeight / 2);
      break;
    }
  }

  // Header last, so scrolled rows never cover it.
  c.fillRect(0, 0, w, kHeaderHeight, TFT_BLACK);
  c.setFont(&fonts::FreeSans9pt7b);
  c.setTextDatum(middle_left);
  c.setTextColor(TFT_LIGHTGREY);
  c.drawString("< Back", 12, kHeaderHeight / 2);
  c.setTextDatum(middle_center);
  c.setTextColor(TFT_DARKGREY);
  c.drawString(step == Step::Lights ? groups[selected].name : String("Wake-up light"), w / 2,
               kHeaderHeight / 2);
  c.drawFastHLine(0, kHeaderHeight, w, kLine);
  c.pushSprite(0, 0);
}

}  // namespace

void enter() {
  hue::loadCredentials();
  groups.clear();
  lights.clear();
  rediscovered = false;
  testText = "";
  wifilink::request();
  go(Step::WaitWifi);
}

void exit() { wifilink::release(); }

bool update(M5Canvas& canvas) {
  if (!handleTouch()) return false;
  draw(canvas);
  if (testPending) {
    runSunTest();
    testPending = false;
  }
  if (stepDrawn) {
    advance();
  } else {
    stepDrawn = true;
  }
  return true;
}

}  // namespace huescreen
