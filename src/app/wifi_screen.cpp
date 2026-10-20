#include "wifi_screen.h"

#include "wifi_link.h"

namespace wifiscreen {
namespace {

constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 44;
constexpr int kBarHeight = 44;
constexpr int8_t kTxPowers[] = {78, 60, 44, 34};  // 19.5, 15, 11, 8.5 dBm

bool holding = false;      // this screen holds a Wi-Fi request
bool scanPending = false;  // scan runs after "Scanning..." was drawn
String scanText;

void reconnect() {
  if (holding) wifilink::release();
  wifilink::request();
  holding = true;
  scanText = "";
}

void cycleProtocol() {
  auto s = wifilink::settings();
  s.protocol = (wifilink::Protocol)(((int)s.protocol + 1) % 3);
  wifilink::setSettings(s);
  if (holding) reconnect();
}

void cycleTxPower() {
  auto s = wifilink::settings();
  size_t next = 0;
  for (size_t i = 0; i < sizeof(kTxPowers); ++i) {
    if (kTxPowers[i] == s.txPower) next = (i + 1) % sizeof(kTxPowers);
  }
  s.txPower = kTxPowers[next];
  wifilink::setSettings(s);
  if (holding) reconnect();
}

void runScan() {
  int rssi = 0, channel = 0;
  if (wifilink::scanConfigured(rssi, channel)) {
    scanText = String("Scan: ") + rssi + " dBm, channel " + channel;
  } else {
    scanText = "Scan: network not visible";
  }
}

// Returns false when "back" was tapped.
bool handleTouch(const M5Canvas& canvas) {
  if (M5.Touch.getCount() == 0) return true;
  auto t = M5.Touch.getDetail(0);
  if (!t.wasClicked()) return true;

  const int h = canvas.height();
  if (t.y < kHeaderHeight) return t.x >= 110;  // back
  if (t.y < kHeaderHeight + kRowHeight) {
    cycleProtocol();
  } else if (t.y < kHeaderHeight + 2 * kRowHeight) {
    cycleTxPower();
  } else if (t.y >= h - kBarHeight) {
    if (t.x < canvas.width() / 2) {
      reconnect();
    } else if (!wifilink::connecting()) {
      scanPending = true;
    }
  }
  return true;
}

void drawRow(M5Canvas& canvas, int y, const char* label, const String& value) {
  const int w = canvas.width();
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.drawString(label, 12, y + kRowHeight / 2);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(TFT_SKYBLUE);
  canvas.drawString(value, w - 12, y + kRowHeight / 2);
  canvas.drawFastHLine(0, y + kRowHeight, w, 0x2104);
}

void draw(M5Canvas& canvas) {
  const int w = canvas.width();
  const int h = canvas.height();
  canvas.fillScreen(TFT_BLACK);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(TFT_SKYBLUE);
  canvas.drawString("< Back", 10, kHeaderHeight / 2);
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.drawString("Wi-Fi test", w - 10, kHeaderHeight / 2);
  canvas.drawFastHLine(0, kHeaderHeight, w, TFT_DARKGREY);

  const auto& s = wifilink::settings();
  drawRow(canvas, kHeaderHeight, "Standard (tap)", wifilink::protocolName(s.protocol));
  drawRow(canvas, kHeaderHeight + kRowHeight, "TX power (tap)", String(s.txPower / 4.0f, 1) + " dBm");

  // Status
  const auto& st = wifilink::stats();
  String line1, line2;
  uint16_t color = TFT_LIGHTGREY;
  if (wifilink::connected()) {
    line1 = String("Connected  ") + wifilink::rssi() + " dBm";
    line2 = String("in ") + String(st.connectMs / 1000.0f, 1) + " s, " + st.attempts + " attempt(s)";
    color = TFT_GREEN;
  } else if (wifilink::connecting()) {
    line1 = String("Connecting  ") + st.connectMs / 1000 + " s  (try " + st.attempts + ")";
    line2 = String("last: ") + wifilink::reasonName(st.lastReason);
    color = TFT_YELLOW;
  } else if (wifilink::failed()) {
    line1 = "Failed";
    line2 = String("last: ") + wifilink::reasonName(st.lastReason);
    color = TFT_RED;
  } else {
    line1 = holding ? "Off" : "Tap Connect to test";
  }
  if (scanPending) line2 = "Scanning...";
  else if (!scanText.isEmpty()) line2 = scanText;

  int statusY = kHeaderHeight + 2 * kRowHeight;
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSans12pt7b);
  canvas.setTextColor(color);
  canvas.drawString(line1, w / 2, statusY + 20);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(TFT_LIGHTGREY);
  canvas.drawString(line2, w / 2, statusY + 48);

  // Touch bar: Connect | Scan
  int barY = h - kBarHeight;
  canvas.drawFastHLine(0, barY, w, 0x2104);
  canvas.drawFastVLine(w / 2, barY, kBarHeight, 0x2104);
  canvas.setTextColor(TFT_SKYBLUE);
  canvas.drawString(holding ? "Reconnect" : "Connect", w / 4, barY + kBarHeight / 2);
  canvas.drawString("Scan", w * 3 / 4, barY + kBarHeight / 2);

  canvas.pushSprite(0, 0);
}

}  // namespace

void enter() {
  holding = false;
  scanPending = false;
  scanText = "";
}

void exit() {
  if (holding) wifilink::release();
  holding = false;
}

bool update(M5Canvas& canvas) {
  if (!handleTouch(canvas)) return false;
  static uint32_t lastDraw = 0;
  if (scanPending) {
    draw(canvas);  // show "Scanning..." before the blocking scan
    runScan();
    scanPending = false;
  }
  if (millis() - lastDraw >= 200) {
    lastDraw = millis();
    draw(canvas);
  }
  return true;
}

}  // namespace wifiscreen
