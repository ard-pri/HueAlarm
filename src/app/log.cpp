#include "log.h"

#include <esp_attr.h>
#include <time.h>

LogBuffer Log;

namespace {

constexpr size_t kSize = 4096;  // RTC slow memory is small (8 KB in total)
constexpr uint32_t kMagic = 0x4C4F4721;

// Not initialised at start-up, so the content survives deep sleep.
RTC_NOINIT_ATTR char buffer[kSize];
RTC_NOINIT_ATTR uint32_t head;
RTC_NOINIT_ATTR uint32_t wrapped;
RTC_NOINIT_ATTR uint32_t magic;

void append(uint8_t c) {
  if (magic != kMagic || head >= kSize) {  // first start after power-on
    head = 0;
    wrapped = 0;
    magic = kMagic;
  }
  buffer[head] = (char)c;
  head = (head + 1) % kSize;
  if (head == 0) wrapped = 1;
}

}  // namespace

size_t LogBuffer::write(uint8_t c) {
  if (lineStart_) {
    // Prefix each buffered line with the local time (or uptime if the clock is not set).
    char stamp[24];
    time_t now = time(nullptr);
    if (now > 1700000000) {
      tm local;
      localtime_r(&now, &local);
      strftime(stamp, sizeof(stamp), "%d.%m %H:%M:%S ", &local);
    } else {
      snprintf(stamp, sizeof(stamp), "+%lus ", (unsigned long)(millis() / 1000));
    }
    for (const char* p = stamp; *p; ++p) append(*p);
    lineStart_ = false;
  }
  append(c);
  if (c == '\n') lineStart_ = true;
  Serial.write(c);
  return 1;
}

size_t LogBuffer::write(const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) write(data[i]);
  return size;
}

void LogBuffer::dump(Print& out) {
  out.println("----- log buffer -----");
  if (magic == kMagic && head < kSize) {
    if (wrapped) out.write((const uint8_t*)buffer + head, kSize - head);
    out.write((const uint8_t*)buffer, head);
  }
  out.println("----- end of log -----");
}
