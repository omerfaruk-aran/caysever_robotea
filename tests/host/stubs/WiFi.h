#pragma once
// Arduino WiFi kütüphanesinin taklidi: bileşen yalnız WiFi.onEvent ile iki olayı dinliyor.
// Testler olayı host_fire() ile kendisi tetikler.
#include <functional>
#include <vector>

typedef int arduino_event_id_t;
struct arduino_event_info_t
{
};
enum
{
  ARDUINO_EVENT_WIFI_STA_GOT_IP = 1,
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 2
};

class WiFiClass
{
public:
  template <typename F>
  void onEvent(F &&f) { this->handlers_.emplace_back(std::forward<F>(f)); }
  void host_fire(arduino_event_id_t ev)
  {
    for (auto &h : this->handlers_)
      h(ev, arduino_event_info_t{});
  }

protected:
  std::vector<std::function<void(arduino_event_id_t, arduino_event_info_t)>> handlers_;
};

inline WiFiClass WiFi;
