#pragma once
#include "esphome.h"
#include <string>
#include <functional>
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <esphome/core/component.h>
#include <esphome/core/log.h>

namespace esphome
{
  namespace caysever_robotea
  {
    struct Modlar
    {
      bool su_kaynatma;
      bool cay_demleme;
      bool filtre_kahve;
      bool mama_suyu;
    };

    enum KettleDurumu
    {
      NORMAL,
      KORUMA,
      KRITIK
    };

    enum SuKaynatmaDurumu
    {
      SU_KAYNATMA_KAPALI,
      SU_KAYNATMA_HAZIRLIK,
      SU_KAYNATMA_SICAKLIK_KORUMA
    };

    enum MamaSuyuDurumu
    {
      MAMA_SUYU_KAPALI,
      MAMA_SUYU_HAZIRLIK,
      MAMA_SUYU_SICAKLIK_KORUMA
    };

    enum CayDemlemeDurumu
    {
      DEMLEME_KAPALI,
      DEMLEME_HAZIRLIK,
      DEMLEME_BASLADI,
      DEMLEME_SICAKLIK_KORUMA
    };

    enum ActiveMode
    {
      MODE_NONE,
      MODE_SU_KAYNATMA,
      MODE_MAMA_SUYU,
      MODE_CAY_DEMLEME
    };

    class CayseverRobotea : public Component
    {
    public:
      void set_modlar(const Modlar &modlar) { this->modlar_ = modlar; }
      void set_mode_sensor(text_sensor::TextSensor *mode_sensor) { this->mode_sensor_ = mode_sensor; }
      void set_kettle_state_sensor(text_sensor::TextSensor *kettle_state_sensor) { this->kettle_state_sensor_ = kettle_state_sensor; }
      void set_mode(ActiveMode new_mode, int press_count);

      void set_ntc_sensor(sensor::Sensor *sensor) { this->ntc_sensor_ = sensor; }
      void set_su_kaynatma_switch(switch_::Switch *su_kaynatma_switch);
      void set_mama_suyu_switch(switch_::Switch *mama_suyu_switch);
      void set_cay_demleme_select(select::Select *cay_demleme_select);
      void set_buton_sesi_switch(switch_::Switch *buton_sesi_switch);
      void set_konusma_sesi_switch(switch_::Switch *konusma_sesi_switch);
      void set_su_kontrol_switch(switch_::Switch *su_kontrol_switch);

      void handle_global_state_reset();
      void reset_all_operations(bool global_reset);
      void visual_feedback_demleme_level(int level); // Görsel geri bildirim

      void setup() override;
      void loop() override;

    protected:
      sensor::Sensor *ntc_sensor_ = nullptr; // NTC sensörü (ESPHome'dan bağlanacak)
      switch_::Switch *su_kaynatma_switch_ = nullptr;
      switch_::Switch *mama_suyu_switch_ = nullptr;
      switch_::Switch *buton_sesi_switch_ = nullptr;
      switch_::Switch *konusma_sesi_switch_ = nullptr;
      switch_::Switch *su_kontrol_switch_ = nullptr;

      select::Select *cay_demleme_select_ = nullptr;
      std::string cay_demleme_state_ = "OFF";

      Modlar modlar_; // Modlar struct'ı burada saklanacak
      ActiveMode current_mode_{MODE_NONE};
      text_sensor::TextSensor *mode_sensor_{nullptr};
      text_sensor::TextSensor *kettle_state_sensor_{nullptr};
      const char *active_mode_to_string(ActiveMode mode);

      SuKaynatmaDurumu su_kaynatma_durumu_;
      MamaSuyuDurumu mama_suyu_durumu_;
      CayDemlemeDurumu cay_demleme_durumu_;
      KettleDurumu kettle_durumu_ = NORMAL;
      KettleDurumu previous_mode_ = NORMAL;

      void publish_mode_();
      void publish_kettle_state_();
      void update_su_kaynatma(bool su_kaynatma);
      void update_mama_suyu(bool mama_suyu);
      void update_cay_demleme(const std::string &level);

      void led_blink(int pin, int times, int delay_ms);
      void on_wifi_connected();    // Wi-Fi bağlantısı sağlandığında
      void on_wifi_disconnected(); // Wi-Fi bağlantısı kesildiğinde
      void handle_touch_input();
      void handle_touch_input_food_water();
      void handle_touch_input_boiling_water();
      void handle_touch_input_brew_tea();
      void handle_touch_input_toggle_button_sound();
      void handle_touch_input_toggle_speak_sound();
      void check_water_level();
      void handle_protection_mode_leds();
      void handle_critical_mode_leds();
      void handle_exit_critical_mode();
      void control_led(int button_index, bool is_white = false);  // LED kontrol fonksiyonu
      void activate_sound(const std::map<int, bool> &pin_states); // Ses kontrol fonksiyonu
      void play_button_sound();
      void play_mama_suyu_hazir_sound();
      void play_cay_demleme_start_sound();
      void play_cay_demleme_done_sound();
      void play_filtre_kahve_hazirlaniyor_sound();
      void play_filtre_kahve_done_sound();
      void play_su_kaynadi_sound();

      void handle_mama_suyu_hazirla(); // Mama suyu hazırlama fonksiyonu
      void handle_su_kaynatma();       // Su kaynatma işlemini yönetecek fonksiyon
      void handle_cay_demleme();       // Çay demleme işlemi (su kaynatma + demleme)
      void maintain_temperature(float min, float max);

      // Röle ve sensör pinleri
      int relay_pin_ = 17;         // Su kaynatma rölesi GPIO17
      int demleme_relay_pin_ = 18; // Demleme rölesi GPIO18
      unsigned long current_time_;

      unsigned long demleme_start_time_ = 0; // Demleme işlemi başlangıç zamanı
      unsigned long demleme_end_time_ = 0;   // Demleme işlemi bitiş zamanı
      unsigned long demled_start_time_ = 0;  // DemLED başlangıç zamanı
      bool demled_active_ = false;           // DemLED aktif mi?
      unsigned int demleme_suresi_ = 0;      // Demleme Süresi
      int demlenme_seviyesi_ = 0;            // Çay demleme seviyesi
      bool manual_exit = false;

      unsigned long kritik_sound_start_time_ = 0;
      bool kritik_sound_active_ = false;

      // LED'lerin önceki durumlarını saklamak için değişkenler
      bool bayled_previous_state = false;           // BayLED'in önceki durumu
      bool demled_previous_state = false;           // DemLED'in önceki durumu
      bool touch0_led_previous_state = false;       // Tuş 1'in önceki led durumu
      bool touch0_color_led_previous_state = false; // Tuş 1'in önceki led rengi durumu

      // Zamanlama ve röle durumları
      unsigned long last_relay_toggle_time_ = 0;   // Son röle değişim zamanı
      const unsigned long relay_wait_time_ = 5000; // Röle bekleme süresi (ms)
      bool relay_active_ = false;                  // Röle durumu (aktif/pasif)
      bool dem_relay_active_ = false;              // Demleme Röle durumu (aktif/pasif)
      bool led_white_active_ = false;

      // LED ve Tuş Ayarları
      int bay_led_pin_ = 22;                  // GPIO22 (Bayat LED)
      int dem_led_pin_ = 21;                  // GPIO21 (Demleme LED)
      int touch_pins_[4] = {12, 14, 27, 33};  // Dokunmatik tuş pinleri
      int led_pins_[5] = {15, 25, 13, 5, 26}; // LED pinleri
      int sound_pins_[3] = {4, 19, 32};       // Ses çıkışı pinleri (GPIO4, GPIO19, GPIO32)

      // Durum ve tuş verileri
      bool touch_states_[4] = {false, false, false, false};          // Tuşların durumları
      bool previous_touch_states_[4] = {false, false, false, false}; // Önceki durum

      void on_su_kaynatma_change(bool state)
      {
        // Eğer durum zaten aynıysa, işlem yapma (mükerrer işlem önlenir)
        if (this->touch_states_[2] == state)
        {
          ESP_LOGI("CayseverRobotea", "Durum zaten %s, işlem yapılmadı.",
                   state ? "aktif" : "pasif");
          return;
        }

        ESP_LOGI("CayseverRobotea", "Su Kaynatma Switch durumu değişti: %s", state ? "ON" : "OFF");

        this->play_button_sound();
        this->update_su_kaynatma(state);
      }

      void on_mama_suyu_change(bool state)
      {
        if (this->touch_states_[0] == state)
        {
          ESP_LOGI("CayseverRobotea", "Durum zaten %s, işlem yapılmadı.",
                   state ? "aktif" : "pasif");
          return;
        }

        ESP_LOGI("CayseverRobotea", "Mama Suyu Switch durumu değişti: %s", state ? "ON" : "OFF");

        this->play_button_sound();
        this->update_mama_suyu(state);
      }
      void on_cay_demleme_change(const std::string &level)
      {
        if (this->cay_demleme_state_ == level)
        {
          ESP_LOGI("CayseverRobotea", "Eşitlik sağlandı => return!");
          return;
        }
        else
        {
          ESP_LOGW("CayseverRobotea", "Eşitlik SAĞLANMADI => %s / %s farkli", this->cay_demleme_state_.c_str(), level.c_str());
        }

        ESP_LOGI("CayseverRobotea", "Çay demleme seviyesi güncellendi: %s", level.c_str());
        this->play_button_sound();
        this->update_cay_demleme(level);
      }
    };

  } // namespace caysever_robotea
} // namespace esphome
