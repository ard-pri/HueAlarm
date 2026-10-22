#include "power.h"

#include <esp_sleep.h>
#include <esp_system.h>

#include "config.h"
#include "log.h"

// The Arduino core tests all 8 MB of PSRAM on every start, ~0.4 s before setup().
// Every wake-up is a start, so skip the test (the PSRAM is still initialised).
extern "C" bool testSPIRAM(void) { return true; }

namespace power {
namespace {

constexpr uint8_t kAxpAddr = 0x34;    // AXP2101 power chip (Core2 v1.1)
constexpr uint8_t kInaAddr = 0x40;    // INA3221 current monitor
constexpr uint8_t kTouchAddr = 0x38;  // FT6336U touch controller
constexpr uint32_t kI2cFreq = 400000;

// AXP2101 LDO enable register 0x90 on the Core2 v1.1.
constexpr uint8_t kAldo3 = 0x04;  // speaker enable
constexpr uint8_t kBldo2 = 0x20;  // 5 V boost enable
constexpr uint8_t kDldo1 = 0x80;  // vibration motor

// The deep sleep timer runs on an RC oscillator that can drift slow by ~3-4 %, so
// sleeps over a minute end 5 % early and the rest is slept again (the clock is
// re-read from the RTC on every start).
constexpr time_t kMaxSleepSeconds = 3 * 3600;
// The RTC countdown that switches the device on is limited to 255 minutes.
constexpr time_t kMaxPowerOffSeconds = 4 * 3600;

bool screenIsOn = false;

bool onUsb() { return M5.Power.getVBUSVoltage() > 4000; }

// Everything that is not needed to wake up on touch: speaker, 5 V boost,
// vibration motor and the current monitor. The LCD/touch supply stays on.
void reduceSleepCurrent() {
  M5.Speaker.end();
  M5.Power.setExtOutput(false);
  // The register settings below are for the Core2 v1.1 (AXP2101 + INA3221) only.
  if (M5.getBoard() != m5::board_t::board_M5StackCore2 ||
      M5.Power.getType() != m5::Power_Class::pmic_axp2101) {
    return;
  }
  uint8_t ldo = M5.In_I2C.readRegister8(kAxpAddr, 0x90, kI2cFreq);
  M5.In_I2C.writeRegister8(kAxpAddr, 0x90, ldo & ~(kAldo3 | kBldo2 | kDldo1), kI2cFreq);
  uint8_t cfg[2];
  if (M5.In_I2C.readRegister(kInaAddr, 0x00, cfg, 2, kI2cFreq)) {
    cfg[1] &= ~0x07;  // INA3221 operating mode: power-down
    M5.In_I2C.writeRegister(kInaAddr, 0x00, cfg, 2, kI2cFreq);
  }
  // FT6336U power mode register 0xA5: 1 = monitor mode (slow scanning, still wakes).
  M5.In_I2C.writeRegister8(kTouchAddr, 0xA5, 0x01, kI2cFreq);
}

[[noreturn]] void deepSleep(time_t seconds) {
  reduceSleepCurrent();
  Serial.flush();
  M5.Power.deepSleep(seconds > 0 ? (uint64_t)seconds * 1000000ULL : M5.Power.sleep_no_timer, true);
  esp_restart();  // not reached
}

}  // namespace

void configure(m5::M5Unified::config_t& cfg) {
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;  // not used; probing it costs start-up time
  cfg.internal_mic = false;
  // The LCD keeps its content in deep sleep; skipping its reset saves ~70 ms. After
  // a power-on it holds random content and must be cleared (or it flashes grey).
  cfg.clear_display = esp_reset_reason() != ESP_RST_DEEPSLEEP;
  // M5.begin() restores M5.Display.getBrightness() (default 128) at the end of its
  // initialisation; zero it beforehand so the backlight never flashes during boot.
  M5.Display.setBrightness(0);
}

void begin() {
  M5.Display.setBrightness(0);
  if (bootReason() == Boot::Timer) M5.Display.sleep();
  // The touch controller was put into monitor mode for the sleep and keeps it
  // through the restart; monitor mode scans slowly, which makes dragging sluggish.
  if (M5.getBoard() == m5::board_t::board_M5StackCore2) {
    M5.In_I2C.writeRegister8(kTouchAddr, 0xA5, 0x00, kI2cFreq);  // active mode
  }
}

Boot bootReason() {
  static int cached = -1;
  if (cached >= 0) return (Boot)cached;
  Boot boot = Boot::Restart;
  esp_reset_reason_t reset = esp_reset_reason();
  if (reset == ESP_RST_DEEPSLEEP) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    boot = cause == ESP_SLEEP_WAKEUP_EXT0 ? Boot::Touch
         : cause == ESP_SLEEP_WAKEUP_TIMER ? Boot::Timer : Boot::Restart;
  } else if (reset == ESP_RST_POWERON) {
    // Switched on either by the side button or by the RTC alarm.
    boot = M5.Rtc.getIRQstatus() ? Boot::Timer : Boot::Button;
    M5.Rtc.clearIRQ();
    M5.Rtc.disableIRQ();
  }
  cached = (int)boot;
  return boot;
}

const char* bootName(Boot boot) {
  switch (boot) {
    case Boot::Touch:   return "touch";
    case Boot::Button:  return "button";
    case Boot::Timer:   return "timer";
    case Boot::Restart: return "restart";
  }
  return "?";
}

void screenOn() {
  if (screenIsOn) return;
  M5.Display.wakeup();
  M5.Display.waitDisplay();
  M5.Display.setBrightness(HUEALARM_BRIGHTNESS);
  screenIsOn = true;
}

void screenOff() {
  if (!screenIsOn) return;
  M5.Display.sleep();
  screenIsOn = false;
}

bool isScreenOn() { return screenIsOn; }

[[noreturn]] void sleepUntil(time_t wakeAt) {
  time_t now = time(nullptr);
  time_t remaining = wakeAt ? max<time_t>(wakeAt - now, 1) : 0;
  M5.Display.sleep();

#if HUEALARM_WAKE_MODE == HUEALARM_WAKE_BUTTON
  if (!onUsb()) {
    // Completely off. The RTC countdown switches the device on for the next alarm
    // step (in steps of at most 4 h), the side button switches it on to look.
    Log.printf("[power] off, %s, battery %d mV\n",
               remaining ? (String(remaining) + " s until the next step").c_str() : "no alarm",
               M5.Power.getBatteryVoltage());
    Serial.flush();
    if (remaining) M5.Power.timerSleep((int)min(remaining, kMaxPowerOffSeconds));
    M5.Power.powerOff();
    esp_restart();  // not reached
  }
  // On USB the power chip cannot switch off: deep sleep instead.
#endif

  time_t seconds = remaining;
  if (seconds > 60) seconds = min(seconds - seconds / 20, kMaxSleepSeconds);
  Log.printf("[power] deep sleep %s, battery %d mV\n",
             seconds ? (String(seconds) + " s").c_str() : "until touch",
             M5.Power.getBatteryVoltage());
  deepSleep(seconds);
}

void logStatus() {
  static constexpr const char* kPmics[] = {"unknown", "ADC", "AXP192", "IP5306", "AXP2101",
                                           "AW32001", "M5PM1"};
  int type = (int)M5.Power.getType();
  Log.printf("[power] PMIC %s, battery %d mV %ld%% %ld mA, VBUS %d mV, wake mode %s, boot %s\n",
             type < (int)(sizeof(kPmics) / sizeof(kPmics[0])) ? kPmics[type] : "?",
             M5.Power.getBatteryVoltage(), (long)M5.Power.getBatteryLevel(),
             (long)M5.Power.getBatteryCurrent(), M5.Power.getVBUSVoltage(),
             HUEALARM_WAKE_MODE == HUEALARM_WAKE_BUTTON ? "button" : "touch",
             bootName(bootReason()));
}

}  // namespace power
