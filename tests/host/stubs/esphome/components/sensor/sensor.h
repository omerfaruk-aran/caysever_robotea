#pragma once
#include "esphome.h"

namespace esphome
{
  namespace sensor
  {
    // Gerçek Sensor: state ilk yayına kadar NAN; publish_state değeri yazar ve geri çağrıları çağırır.
    class Sensor
    {
    public:
      float state{NAN};
      std::vector<float> history; // testler için

      void publish_state(float v)
      {
        this->state = v;
        this->history.push_back(v);
        for (auto &cb : this->callbacks_)
          cb(v);
      }
      template <typename F>
      void add_on_state_callback(F &&f) { this->callbacks_.emplace_back(std::forward<F>(f)); }

    protected:
      std::vector<std::function<void(float)>> callbacks_;
    };
  } // namespace sensor
} // namespace esphome
