#pragma once
#include "esphome.h"

namespace esphome
{
  namespace switch_
  {
    // Gerçek Switch::publish_state aynı değerin tekrarını yutar (publish_dedup_): geri çağrılar
    // yalnız ilk yayında ve değer değiştiğinde çalışır. Bileşenin mantığı buna dayandığı için birebir taklit.
    class Switch
    {
    public:
      bool state{false};
      std::vector<bool> history; // testler için (yalnız gerçekten yayınlananlar)

      void publish_state(bool s)
      {
        if (this->has_published_ && this->last_published_ == s)
          return;
        this->has_published_ = true;
        this->last_published_ = s;
        this->state = s;
        this->history.push_back(s);
        for (auto &cb : this->callbacks_)
          cb(s);
      }
      template <typename F>
      void add_on_state_callback(F &&f) { this->callbacks_.emplace_back(std::forward<F>(f)); }

    protected:
      bool has_published_{false};
      bool last_published_{false};
      std::vector<std::function<void(bool)>> callbacks_;
    };
  } // namespace switch_
} // namespace esphome
