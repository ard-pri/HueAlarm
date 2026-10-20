// HueAlarm - step 1: hardware check for M5Stack CoreS3 / Core2.
// Shows and logs: board, battery, RTC, IMU, touch, buttons, proximity/light (CoreS3).
// Touch the screen (or press A/B/C on Core2) to play a test tone.

#include <M5Unified.h>

namespace {

constexpr uint8_t kLtr553Addr = 0x23;  // CoreS3 proximity / ambient light sensor
constexpr uint32_t kI2cFreq = 400000;

M5Canvas canvas(&M5.Display);
bool hasLtr553 = false;

const char* boardName(m5::board_t board) {
  switch (board) {
    case m5::board_t::board_M5StackCoreS3:   return "CoreS3";
    case m5::board_t::board_M5StackCoreS3SE: return "CoreS3 SE";
    case m5::board_t::board_M5StackCore2:    return "Core2";
    default:                                 return "unknown";
  }
}

const char* chargeState(m5::Power_Class::is_charging_t state) {
  switch (state) {
    case m5::Power_Class::is_charging:    return "charging";
    case m5::Power_Class::is_discharging: return "discharging";
    default:                              return "unknown";
  }
}

void logI2cDevices() {
  bool found[128] = {};
  M5.In_I2C.scanID(found);
  Serial.print("Internal I2C devices:");
  for (int addr = 0; addr < 128; ++addr) {
    if (found[addr]) Serial.printf(" 0x%02X", addr);
  }
  Serial.println();
}

void initLtr553() {
  hasLtr553 = M5.In_I2C.scanID(kLtr553Addr);
  if (!hasLtr553) return;
  M5.In_I2C.writeRegister8(kLtr553Addr, 0x80, 0x01, kI2cFreq);  // ALS_CONTR: active, gain 1x
  M5.In_I2C.writeRegister8(kLtr553Addr, 0x81, 0x03, kI2cFreq);  // PS_CONTR: active
}

// ALS channel 0 (visible + IR) raw count and proximity raw count (0..2047).
void readLtr553(uint16_t& als, uint16_t& ps) {
  uint8_t alsBuf[4] = {};  // CH1 low, CH1 high, CH0 low, CH0 high (must be read in one go)
  uint8_t psBuf[2] = {};
  M5.In_I2C.readRegister(kLtr553Addr, 0x88, alsBuf, sizeof(alsBuf), kI2cFreq);
  M5.In_I2C.readRegister(kLtr553Addr, 0x8D, psBuf, sizeof(psBuf), kI2cFreq);
  als = alsBuf[2] | (alsBuf[3] << 8);
  ps = psBuf[0] | ((psBuf[1] & 0x07) << 8);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  M5.Display.setBrightness(128);
  M5.Speaker.setVolume(128);

  canvas.setPsram(true);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  canvas.setFont(&fonts::FreeSans9pt7b);

  Serial.printf("\nHueAlarm hardware check - board: %s\n", boardName(M5.getBoard()));
  logI2cDevices();
  initLtr553();
  Serial.printf("IMU: %s, RTC: %s, LTR-553: %s\n",
                M5.Imu.isEnabled() ? "ok" : "missing",
                M5.Rtc.isEnabled() ? "ok" : "missing",
                hasLtr553 ? "ok" : "missing");

  M5.Speaker.tone(2000, 100);
}

void loop() {
  M5.update();

  // Test tone on touch or button press.
  bool beep = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed() ||
              M5.BtnPWR.wasClicked();
  int touchX = -1, touchY = -1;
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail(0);
    touchX = t.x;
    touchY = t.y;
    beep |= t.wasPressed();
  }
  if (beep) M5.Speaker.tone(1500, 80);

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw < 200) return;
  lastDraw = millis();

  float ax = 0, ay = 0, az = 0;
  if (M5.Imu.update()) M5.Imu.getAccel(&ax, &ay, &az);

  auto dt = M5.Rtc.getDateTime();

  uint16_t als = 0, ps = 0;
  if (hasLtr553) readLtr553(als, ps);

  int32_t batLevel = M5.Power.getBatteryLevel();
  int16_t batMv = M5.Power.getBatteryVoltage();
  int32_t batMa = M5.Power.getBatteryCurrent();
  int16_t vbusMv = M5.Power.getVBUSVoltage();
  const char* charge = chargeState(M5.Power.isCharging());

  canvas.fillScreen(TFT_BLACK);
  canvas.setTextColor(TFT_WHITE);
  canvas.setCursor(8, 8);
  canvas.printf("HueAlarm HW check: %s\n", boardName(M5.getBoard()));
  canvas.printf("RTC  %04d-%02d-%02d %02d:%02d:%02d\n", dt.date.year, dt.date.month,
                dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds);
  canvas.printf("Bat  %ld%%  %d mV  %ld mA\n", (long)batLevel, batMv, (long)batMa);
  canvas.printf("     %s, VBUS %d mV\n", charge, vbusMv);
  canvas.printf("IMU  x%.2f y%.2f z%.2f g\n", ax, ay, az);
  if (hasLtr553) canvas.printf("Light %u  Prox %u\n", als, ps);
  canvas.printf("Touch %d,%d\n", touchX, touchY);
  canvas.setTextColor(TFT_DARKGREY);
  canvas.println("Touch screen = test tone");
  canvas.pushSprite(0, 0);

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 1000) {
    lastLog = millis();
    Serial.printf("bat=%ld%% %dmV %ldmA %s vbus=%dmV acc=%.2f,%.2f,%.2f als=%u ps=%u\n",
                  (long)batLevel, batMv, (long)batMa, charge, vbusMv, ax, ay, az, als, ps);
  }
}
