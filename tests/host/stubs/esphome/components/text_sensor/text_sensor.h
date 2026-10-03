#pragma once
#include "esphome.h"

namespace esphome
{
  namespace text_sensor
  {
    class TextSensor
    {
    public:
      std::string state;
      std::vector<std::pair<uint32_t, std::string>> changes; // testler için: (an, yeni değer), yalnız değişimler

      void publish_state(const std::string &s) { this->publish_state(s.c_str()); }
      void publish_state(const char *s)
      {
        if (this->changes.empty() || this->state != s)
          this->changes.emplace_back(millis(), s);
        this->state = s;
      }
    };
  } // namespace text_sensor
} // namespace esphome
