#pragma once
// Bileşeni bilgisayarda (host) derleyip sınamak için asgari Arduino + ESPHome taklidi.
// Yalnızca caysever_robotea bileşeninin kullandığı kadarı vardır; gerçek ESPHome'un yerine geçmez.

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define CHANGE 3
#define IRAM_ATTR

namespace hoststub
{
  struct State
  {
    uint32_t now_ms = 0;  // millis() bunu döndürür; testler ilerletir
    int pin_level[48];    // digitalWrite / digitalRead
    bool verbose = false; // true ise ESP_LOGx satırları basılır
    std::function<void()> on_delay; // delay() çağrıldığında (ör. açılıştaki LED yanıp sönmesi) testin bakabilmesi için
    void (*isr[48])() = {};         // attachInterrupt ile bağlanan kesme işlevleri; testler kenar üretmek için çağırır
    State()
    {
      for (auto &p : pin_level)
        p = LOW;
    }
  };

  inline State &st()
  {
    static State s;
    return s;
  }

  inline void logf(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
  inline void logf(char level, const char *tag, const char *fmt, ...)
  {
    if (!st().verbose && level != 'E')
      return;
    va_list ap;
    va_start(ap, fmt);
    printf("    [%8.1fs][%c][%s] ", st().now_ms / 1000.0, level, tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
  }
} // namespace hoststub

inline uint32_t millis() { return hoststub::st().now_ms; }
inline void delay(uint32_t ms)
{
  if (hoststub::st().on_delay)
    hoststub::st().on_delay();
  hoststub::st().now_ms += ms;
}
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int level) { hoststub::st().pin_level[pin] = level ? HIGH : LOW; }
inline int digitalRead(int pin) { return hoststub::st().pin_level[pin]; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int pin, void (*fn)(), int) { hoststub::st().isr[pin] = fn; }
inline void detachInterrupt(int pin) { hoststub::st().isr[pin] = nullptr; }

#define ESP_LOGE(tag, ...) hoststub::logf('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) hoststub::logf('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) hoststub::logf('I', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) hoststub::logf('D', tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) hoststub::logf('V', tag, __VA_ARGS__)

#include "esphome/core/component.h"
