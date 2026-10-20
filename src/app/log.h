#pragma once

#include <Arduino.h>

// Log output that goes to USB serial and into a ring buffer in RTC memory, which
// survives deep sleep (not power off). Send 'd' over serial to read it out.
class LogBuffer : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

  // Print the buffered log (oldest first) to `out`.
  void dump(Print& out);

 private:
  bool lineStart_ = true;
};

extern LogBuffer Log;
