#pragma once
#include "esphome.h"

namespace esphome
{
  // ESPHome Component'inin taklidi: yalnız adlı set_timeout ve zamanlayıcının çağrılması.
  class Component
  {
  public:
    virtual ~Component() = default;
    virtual void setup() {}
    virtual void loop() {}

    // Gerçek ESPHome'daki gibi: aynı adlı bekleyen zamanlayıcının yerini alır.
    void set_timeout(const char *name, uint32_t timeout, std::function<void()> &&f)
    {
      std::string n(name);
      for (auto it = this->timeouts_.begin(); it != this->timeouts_.end();)
        it = (it->name == n) ? this->timeouts_.erase(it) : it + 1;
      this->timeouts_.push_back({n, millis() + timeout, std::move(f)});
    }

    // App.loop() içindeki scheduler.call() karşılığı: loop()'tan ÖNCE çağrılır. Çağrı sırasında
    // eklenen zamanlayıcılar bir sonraki tura kalır (gerçek zamanlayıcıdaki gibi).
    void host_run_scheduler()
    {
      std::vector<Timeout> due;
      for (auto it = this->timeouts_.begin(); it != this->timeouts_.end();)
      {
        if (it->due <= millis())
        {
          due.push_back(std::move(*it));
          it = this->timeouts_.erase(it);
        }
        else
          ++it;
      }
      for (auto &t : due)
        t.f();
    }

  protected:
    struct Timeout
    {
      std::string name;
      uint32_t due;
      std::function<void()> f;
    };
    std::vector<Timeout> timeouts_;
  };
} // namespace esphome
