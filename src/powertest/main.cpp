// HueAlarm power test for the M5Stack Core2 v1.1 (AXP2101 + INA3221), on battery.
//
// Round 2: is deep sleep with touch wake cheap enough? After unplugging USB the
// device rests 1 h (the voltage settles), then runs two blocks of kBlockHours;
// the battery voltage is measured under the same conditions at the start and
// end of each block:
//   E  deep sleep, touch wake; speaker, backlight, 5 V boost, vibration supplies
//      and the INA3221 off - only the LCD/touch supplies (ALDO2/ALDO4) stay on
//   F  as E, plus the touch controller forced into its low-power monitor mode
// Round 1 results (4 h blocks, ~4.05 V): A light sleep 2.25, B deep sleep + touch
// 1.25, C deep sleep with all supplies off 0.25, D power off ~0 mV/h.
// Results are kept in flash (deep sleep and power-off restart the firmware) and
// shown on screen; over USB serial, 'd' prints them, 'r' resets the test and
// 'e' / 'f' run a 60 s touch-wake check in mode E / F; 'g' / 'o' measure the
// wake-up time after a touch (deep sleep) / the side button (power off).

#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>

// The Arduino core tests all 8 MB of PSRAM on every start (~0.4 s before setup()).
// Skipping the test keeps the RAM working and makes wake-ups faster ('T' toggles it).
static bool skipPsramTest() {
  Preferences p;
  p.begin("ptest2", true);
  bool skip = p.getBool("nomemtest", true);
  p.end();
  return skip;
}
extern "C" bool testSPIRAM(void) {
  extern bool esp_spiram_test(void);
  return skipPsramTest() ? true : esp_spiram_test();
}

namespace {

constexpr int kBlockHours = 6;
constexpr time_t kBlockSeconds = kBlockHours * 3600;
constexpr uint32_t kUnpluggedMs = 60000;  // USB must be off this long before the test starts
constexpr time_t kRestSeconds = 3600;     // battery voltage settles after charging
constexpr uint8_t kAxpAddr = 0x34;
constexpr uint8_t kInaAddr = 0x40;
constexpr uint32_t kI2cFreq = 400000;
constexpr uint8_t kTouchAddr = 0x38;  // FT6336U
constexpr const char* kPhaseNames[] = {"-", "E deep sleep + touch, rails off",
                                       "F as E + touch monitor mode"};
constexpr int kPhaseCount = 2;

// Persistent test state
int phase = 0;            // 0 = not started, 1..4 = running block, 5 = done
time_t blockStart = 0;
int startMv = 0;
int touchWakes = 0;      // touch wake-ups in the current block (proves touch still works)
String results;           // one line per finished step

time_t now() {
  auto dt = M5.Rtc.getDateTime();
  tm t = {};
  t.tm_year = dt.date.year - 1900;
  t.tm_mon = dt.date.month - 1;
  t.tm_mday = dt.date.date;
  t.tm_hour = dt.time.hours;
  t.tm_min = dt.time.minutes;
  t.tm_sec = dt.time.seconds;
  return mktime(&t);  // TZ is unset (UTC); only differences matter here
}

void load() {
  Preferences p;
  p.begin("ptest2", true);
  phase = p.getInt("phase", 0);
  blockStart = p.getLong("start", 0);
  startMv = p.getInt("mv", 0);
  touchWakes = p.getInt("touches", 0);
  results = p.getString("results", "");
  p.end();
}

void save() {
  Preferences p;
  p.begin("ptest2", false);
  p.putInt("phase", phase);
  p.putLong("start", blockStart);
  p.putInt("mv", startMv);
  p.putInt("touches", touchWakes);
  p.putString("results", results);
  p.end();
}

void addResult(const String& line) {
  results += line + "\n";
  Serial.println(line);
  save();
}

// Battery voltage under identical conditions: awake, display off, Wi-Fi off,
// after a short settling time, averaged.
int measureMv() {
  delay(3000);
  long sum = 0;
  for (int i = 0; i < 20; ++i) {
    sum += M5.Power.getBatteryVoltage();
    delay(100);
  }
  return sum / 20;
}

void axpBit(uint8_t reg, uint8_t mask, bool on) {
  uint8_t value = M5.In_I2C.readRegister8(kAxpAddr, reg, kI2cFreq);
  value = on ? (value | mask) : (value & ~mask);
  M5.In_I2C.writeRegister8(kAxpAddr, reg, value, kI2cFreq);
}

// AXP2101 LDO enable register 0x90 on the Core2 v1.1:
constexpr uint8_t kAldo2 = 0x02;  // LCD + touch reset
constexpr uint8_t kAldo3 = 0x04;  // speaker enable
constexpr uint8_t kAldo4 = 0x08;  // LCD, touch, SD power
constexpr uint8_t kBldo1 = 0x10;  // backlight
constexpr uint8_t kBldo2 = 0x20;  // 5 V boost enable
constexpr uint8_t kDldo1 = 0x80;  // vibration motor

void inaPowerDown() {
  uint8_t cfg[2];
  M5.In_I2C.readRegister(kInaAddr, 0x00, cfg, 2, kI2cFreq);
  cfg[1] &= ~0x07;  // operating mode bits = power-down
  M5.In_I2C.writeRegister(kInaAddr, 0x00, cfg, 2, kI2cFreq);
}

void show(const String& title, const String& body) {
  M5.Display.wakeup();
  M5.Display.setBrightness(80);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setCursor(4, 4);
  M5.Display.println(title);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.println(body);
}

void displayOff() {
  M5.Display.sleep();
  M5.Display.waitDisplay();
}

void startBlock(int next) {
  phase = next;
  displayOff();
  startMv = measureMv();
  blockStart = now();
  touchWakes = 0;
  save();
  Serial.printf("Block %s starts at %d mV\n", kPhaseNames[phase], startMv);
}

void finishBlock() {
  displayOff();
  int endMv = measureMv();
  float hours = (now() - blockStart) / 3600.0f;
  int drop = startMv - endMv;
  addResult(String(kPhaseNames[phase]) + ": " + startMv + " -> " + endMv + " mV, " +
            String(hours, 2) + " h, " + String(drop / max(hours, 0.01f), 2) + " mV/h, " + touchWakes +
            " touch wake(s)");
}

// Deep sleep with everything except the LCD/touch supplies off; touch (GPIO39)
// wakes the ESP32. `touchMonitor` = mode F. Does not return.
void deepSleepMode(bool touchMonitor, time_t seconds) {
  displayOff();
  M5.Speaker.end();
  Serial.flush();
  M5.Power.setExtOutput(false);
  inaPowerDown();
  axpBit(0x90, kAldo3 | kBldo1 | kBldo2 | kDldo1, false);
  if (touchMonitor) {
    // FT6336U power mode register 0xA5: 1 = monitor (slow scanning, still wakes).
    M5.In_I2C.writeRegister8(kTouchAddr, 0xA5, 0x01, kI2cFreq);
  }
  M5.Power.deepSleep((uint64_t)seconds * 1000000ULL, true);
}

// Sleep in the current block's mode until the block ends. Deep sleep and power
// off do not return: the firmware restarts and continues in setup().
void sleepRestOfBlock() {
  time_t left = blockStart + kBlockSeconds - now();
  if (left <= 0) return;
  displayOff();
  M5.Speaker.end();
  Serial.flush();

  deepSleepMode(phase == 2, left);
}

void printResults() {
  Serial.println("----- power test results -----");
  Serial.printf("phase %d (%s)\n", phase, phase <= kPhaseCount ? kPhaseNames[phase] : "done");
  Serial.print(results);
  Serial.println("------------------------------");
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'd') printResults();
    if (c == 'F') {
      Preferences p;
      p.begin("ptest2", false);
      int fast = (p.getInt("fast", 0) + 1) % 3;
      p.putInt("fast", fast);
      p.end();
      Serial.printf("startup variant now %d\n", fast);
    }
    if (c == 'T') {
      Preferences p;
      p.begin("ptest2", false);
      bool skip = !p.getBool("nomemtest", true);
      p.putBool("nomemtest", skip);
      p.end();
      Serial.printf("PSRAM test at start-up: %s\n", skip ? "skipped" : "on");
    }
    if (c == 'x') {
      // Latency of a plain restart (same start-up path as a wake-up, no tap needed).
      Preferences p;
      p.begin("ptest2", false);
      p.putChar("lat", 'x');
      p.end();
      Serial.flush();
      ESP.restart();
    }
    if (c == 'g' || c == 'o') {
      Preferences p;
      p.begin("ptest2", false);
      p.putChar("lat", c);
      p.end();
      if (c == 'g') {
        Serial.println("latency test: tap the screen to wake");
        deepSleepMode(true, 600);
      }
      // Restart into the button test: it waits until USB is unplugged, then switches
      // off; a short press on the side button switches on again.
      Serial.println("latency test: unplug USB, then press the side button briefly");
      Serial.flush();
      ESP.restart();
    }
    if (c == 'e' || c == 'f') {
      // Quick touch test: sleep 60 s in mode E / F; a tap must wake the device.
      Preferences p;
      p.begin("ptest2", false);
      p.putChar("quick", c);
      p.end();
      Serial.printf("touch test %c: tap the screen within 60 s\n", c);
      deepSleepMode(c == 'f', 60);
    }
    if (c == 'r') {
      phase = 0;
      results = "";
      save();
      Serial.println("test reset");
      ESP.restart();
    }
  }
}

}  // namespace

// Button test: wait until USB is unplugged, then switch completely off; a short
// press on the side button switches the device on and measures the start-up.
// (A long press on the Core2 restarts it instead of switching it off.)
void powerOffWhenUnplugged() {
  uint32_t unpluggedSince = 0;
  uint32_t lastShow = 0;
  while (true) {
    bool onUsb = M5.Power.getVBUSVoltage() > 4000;
    if (onUsb) unpluggedSince = 0;
    else if (!unpluggedSince) unpluggedSince = millis();
    if (millis() - lastShow > 500) {
      lastShow = millis();
      show("Button test", onUsb ? "Unplug USB. The device then switches off;\nswitch it on with a short press on the side button."
                                : "Switching off ...");
    }
    if (unpluggedSince && millis() - unpluggedSince > 2000) {
      Serial.flush();
      M5.Power.powerOff();
    }
    delay(50);
  }
}

// Wake-up latency test: draw a clock as fast as possible after a touch wake from
// deep sleep ('g') or after power-on with the side button ('o'), and report how
// long the firmware took (millis() starts after the ~0.2-0.3 s ROM / bootloader).
void latencyTest() {
  Preferences p;
  p.begin("ptest2", false);
  char mode = p.getChar("lat", 0);
  // The button test only counts a real power-on; unplugging USB can cause a
  // brown-out restart first, which must not use up the test.
  if (mode == 'o' && esp_reset_reason() != ESP_RST_POWERON) {
    Serial.printf("button test armed, ignoring restart (reason %d)\n", (int)esp_reset_reason());
    p.end();
    powerOffWhenUnplugged();
  }
  p.remove("lat");
  p.end();
  if (!mode) return;

  uint32_t begun = millis();
  M5.Display.wakeup();
  M5.Display.setBrightness(0);  // backlight stays off until the clock is drawn
  M5.Display.fillScreen(TFT_BLACK);
  auto dt = M5.Rtc.getDateTime();
  char text[8];
  snprintf(text, sizeof(text), "%02d:%02d", dt.time.hours, dt.time.minutes);
  M5.Display.setFont(&fonts::Font7);
  M5.Display.setTextSize(1.8f);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString(text, 160, 100);
  M5.Display.waitDisplay();
  M5.Display.setBrightness(128);
  uint32_t drawn = millis();
  Serial.printf("LATENCY %c: M5.begin done %lu ms, clock drawn %lu ms after start\n", mode,
                (unsigned long)begun, (unsigned long)drawn);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.drawString(String(mode == 'g' ? "touch: " : mode == 'o' ? "button: " : "restart: ") + drawn + " ms + ~0.3 s boot", 160, 200);
  addResult(String("Wake latency ") + (mode == 'g' ? "touch" : "button") + ": clock drawn " + drawn +
            " ms after start (+ ~0.3 s ROM/bootloader)");
  delay(mode == 'g' ? 4000 : 8000);
  if (mode == 'g' && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    // Repeat: back to sleep, so every tap shows the wake-up again ('r' over USB ends it).
    p.begin("ptest2", false);
    p.putChar("lat", 'g');
    p.end();
    deepSleepMode(true, 3600);
  }
}

uint32_t setupStartMs = 0;
uint32_t beginDoneMs = 0;

void setup() {
  setupStartMs = millis();
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  // Start-up variants for the latency test ('F' cycles them):
  // 0 = defaults, 1 = no IMU / mic / speaker, 2 = 1 + no display reset.
  int fast;
  {
    Preferences p;
    p.begin("ptest2", true);
    fast = p.getInt("fast", 0);
    p.end();
  }
  if (fast >= 1) {
    cfg.internal_imu = false;
    cfg.internal_mic = false;
    cfg.internal_spk = false;
  }
  // Skipping the display reset is only safe when the LCD stayed powered (wake from
  // deep sleep); after a power-on it shows random grey content until cleared.
  if (fast >= 2 && esp_reset_reason() != ESP_RST_POWERON) cfg.clear_display = false;
  // Backlight off during start-up: after a power-on the LCD holds random content
  // (a grey flash) until the first frame is drawn.
  M5.Display.setBrightness(0);
  M5.begin(cfg);
  M5.Display.setBrightness(0);
  beginDoneMs = millis();
  Serial.printf("startup variant %d: setup() at %lu ms, M5.begin done at %lu ms\n", fast,
                (unsigned long)setupStartMs, (unsigned long)beginDoneMs);
  load();
  latencyTest();
  Serial.printf("\nPower test, phase %d, wake cause %d\n", phase, (int)esp_sleep_get_wakeup_cause());
  printResults();
  {
    Preferences p;
    p.begin("ptest2", false);
    char quick = p.getChar("quick", 0);
    p.remove("quick");
    p.end();
    if (quick) {
      bool touched = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
      String text = String("Touch test ") + (char)(quick - 32) + ": " +
                    (touched ? "woke by touch - OK" : "NOT woken by touch (timer)");
      addResult(text);
      show(text, "");
      delay(5000);
    }
  }
  if (phase >= 1 && phase <= kPhaseCount && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    ++touchWakes;
    save();
    show("Touch wake OK", String(kPhaseNames[phase]));  // brief proof on screen
    delay(1500);
  }

  if (phase >= 1 && phase <= kPhaseCount) {
    if (now() >= blockStart + kBlockSeconds) {
      finishBlock();
      if (phase < kPhaseCount) {
        startBlock(phase + 1);
      } else {
        phase = kPhaseCount + 1;
        save();
      }
    }
  }
}

void loop() {
  M5.update();
  handleSerial();

  if (phase == 0) {
    // Wait until USB has been unplugged for a minute, then run the test.
    static uint32_t unpluggedSince = 0;
    bool onUsb = M5.Power.getVBUSVoltage() > 4000;
    if (onUsb) unpluggedSince = 0;
    else if (!unpluggedSince) unpluggedSince = millis();
    static uint32_t lastShow = 0;
    if (millis() - lastShow > 1000) {
      lastShow = millis();
      show("Power test", onUsb ? String("Unplug USB to start.\nResults so far:\n") + results
                               : String("Starting in ") + (int)((kUnpluggedMs - (millis() - unpluggedSince)) / 1000) + " s ...");
    }
    if (unpluggedSince && millis() - unpluggedSince > kUnpluggedMs) {
      // Rest in light sleep first: right after charging the voltage keeps falling
      // for a while without load, which would distort the first block.
      Serial.println("resting 1 h");
      Serial.flush();
      displayOff();
      M5.Power.setExtOutput(false);
      time_t restEnd = now() + kRestSeconds;
      while (now() < restEnd) {
        M5.Power.lightSleep((uint64_t)min<time_t>(restEnd - now(), 600) * 1000000ULL, false);
      }
      M5.Power.setExtOutput(true);
      startBlock(1);
    }
    delay(50);
    return;
  }

  if (phase <= kPhaseCount) {
    sleepRestOfBlock();
    // Only light sleep and an early touch / serial wake get here.
    if (now() >= blockStart + kBlockSeconds) {
      finishBlock();
      if (phase < kPhaseCount) {
        startBlock(phase + 1);
      } else {
        phase = kPhaseCount + 1;
        save();
      }
    }
    return;
  }

  // Done: show the results; touch or serial input wake the screen.
  static uint32_t shownAt = 0;
  if (!shownAt) {
    shownAt = millis();
    show("Power test done", results);
  }
  if (millis() - shownAt > 60000) {
    displayOff();
    M5.Power.deepSleep(M5.Power.sleep_no_timer, true);
  }
  delay(50);
}
