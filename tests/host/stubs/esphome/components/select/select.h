#pragma once
#include "esphome.h"

namespace esphome
{
  namespace select
  {
    // Gerçek Select: publish_state tekrarı yutmaz, geri çağrıya seçeneğin indeksini verir;
    // current_option() içerik karşılaştırması yapan StringRef döndürür (burada std::string).
    class Select
    {
    public:
      std::vector<std::string> options{"1/4", "2/4", "3/4", "MAX", "KAPALI"}; // __init__.py'deki liste
      std::vector<std::string> history;                                       // testler için

      void publish_state(const std::string &s) { this->publish_state(s.c_str()); }
      void publish_state(const char *s)
      {
        for (size_t i = 0; i < this->options.size(); i++)
        {
          if (this->options[i] == s)
          {
            this->has_state_ = true;
            this->active_index_ = i;
            this->history.push_back(s);
            for (auto &cb : this->callbacks_)
              cb(i);
            return;
          }
        }
        ESP_LOGE("select", "Invalid option %s", s);
      }
      std::string current_option() const { return this->has_state_ ? this->options[this->active_index_] : std::string(); }

      template <typename F>
      void add_on_state_callback(F &&f) { this->callbacks_.emplace_back(std::forward<F>(f)); }

    protected:
      bool has_state_{false};
      size_t active_index_{0};
      std::vector<std::function<void(size_t)>> callbacks_;
    };
  } // namespace select
} // namespace esphome
