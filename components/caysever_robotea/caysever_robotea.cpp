#include "caysever_robotea.h"
#include "esphome/core/log.h"
#include <WiFi.h>
#include <algorithm>

namespace esphome
{
    namespace caysever_robotea
    {
        static const char *const TAG = "caysever_robotea";

        void CayseverRobotea::setup()
        {
            // LED pinlerini çıkış olarak ayarla
            pinMode(this->bay_led_pin_, OUTPUT);
            pinMode(this->dem_led_pin_, OUTPUT);

            // GPIO22 LED'ini başlangıçta 5 kez yanıp söndür
            this->led_blink(this->bay_led_pin_, 5, 300);

            // Dokunmatik tuş pinlerini giriş olarak ayarla
            for (int i = 0; i < 4; i++)
            {
                pinMode(this->touch_pins_[i], INPUT); // Dokunmatik giriş pini
            }

            // LED pinlerini çıkış olarak ayarla
            for (int i = 0; i < 5; i++)
            {
                pinMode(this->led_pins_[i], OUTPUT);
                digitalWrite(this->led_pins_[i], LOW); // Tüm LED’leri başlangıçta kapalı yap
            }
            // Röle pini çıkış olarak ayarla
            pinMode(this->relay_pin_, OUTPUT);
            if (digitalRead(this->relay_pin_) != LOW)
            {
                digitalWrite(this->relay_pin_, LOW);
            }

            // Demleme rölesi pinini çıkış olarak ayarla ve başlangıçta kapalı yap
            pinMode(this->demleme_relay_pin_, OUTPUT);
            digitalWrite(this->demleme_relay_pin_, LOW);

            // Ses pinlerini çıkış olarak ayarla ve başlangıç durumunu LOW yap
            for (int i = 0; i < 3; i++) // Burada `sound_pins_` 3 elemanlı bir dizi
            {
                pinMode(this->sound_pins_[i], OUTPUT);
                digitalWrite(this->sound_pins_[i], LOW);
            }
            this->current_mode_ = MODE_KAPALI;
            this->publish_mode_();
            // Wi-Fi olaylarını dinle
            WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info)
                         {
                if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
                {
                  this->on_wifi_connected();
                }
                else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
                {
                  this->on_wifi_disconnected();
                } });

            // Başlangıç durumlarını sıfırla
            this->su_kaynatma_durumu_ = SU_KAYNATMA_KAPALI;
            this->mama_suyu_durumu_ = MAMA_SUYU_KAPALI;
            this->cay_demleme_durumu_ = DEMLEME_KAPALI;

            this->kettle_durumu_ = NORMAL;
            this->update_all_sensors();

            this->wl_grace_until_ = this->current_time_ + 15000; // 60sn
        }

        // loop metodu tanımı
        void CayseverRobotea::loop()
        {
            if (this->kettle_durumu_ != NORMAL && this->demleme_fb_.active)
            {
                this->demleme_fb_.active = false;
            }

            this->current_time_ = millis();

            this->handle_touch_input_toggle_button_sound();
            this->handle_touch_input_toggle_speak_sound();

            // Sensör değerlerini kontrol edin
            if (this->ntc_sensor_)
            {
                float temperature = this->ntc_sensor_->state;

                if (temperature < 0.0f) // Anormal sıcaklık değeri
                {
                    if (this->kettle_durumu_ != KORUMA)
                    {
                        ESP_LOGW("CayseverRobotea", "Kettle kaldırıldı veya NTC sensör hatası. Koruma moduna geçiliyor.");
                        // Önceki durumu kaydet
                        this->previous_mode_ = kettle_durumu_;

                        this->kettle_durumu_ = KORUMA;
                        this->update_all_sensors();

                        // LED durumlarını kaydet
                        bayled_previous_state = digitalRead(this->bay_led_pin_);
                        demled_previous_state = digitalRead(this->dem_led_pin_);

                        // DemLED'i kapat
                        if (digitalRead(this->dem_led_pin_) != LOW)
                        {
                            digitalWrite(this->dem_led_pin_, LOW);
                        }

                        // Tüm röleleri kapat
                        if (digitalRead(this->relay_pin_) != LOW)
                        {
                            digitalWrite(this->relay_pin_, LOW);
                        }
                        if (digitalRead(this->demleme_relay_pin_) != LOW)
                        {
                            digitalWrite(this->demleme_relay_pin_, LOW);
                        }
                        this->relay_active_ = false;
                        this->dem_relay_active_ = false;
                    }
                }
                else
                {
                    if (this->kettle_durumu_ == KORUMA)
                    {
                        ESP_LOGI("CayseverRobotea", "Kettle yerine yerleştirildi. İşlemler devam ediyor.");

                        // Önceki duruma dön
                        if (this->previous_mode_ == KRITIK)
                        {
                            ESP_LOGI("CayseverRobotea", "Kritik moda geri dönülüyor.");
                            this->kettle_durumu_ = KRITIK;
                            this->previous_mode_ = NORMAL;
                        }
                        else
                        {
                            ESP_LOGI("CayseverRobotea", "Normal moda geri dönülüyor.");
                            // BayLED ve DemLED'i eski durumlarına döndür
                            digitalWrite(this->bay_led_pin_, bayled_previous_state);
                            digitalWrite(this->dem_led_pin_, demled_previous_state);
                            this->kettle_durumu_ = NORMAL;
                            this->previous_mode_ = NORMAL;
                        }
                        this->update_all_sensors();
                    }
                }
            }
            else
            {
                if (this->kettle_durumu_ != KORUMA)
                {
                    ESP_LOGW("CayseverRobotea", "NTC sensörü bağlı değil!");
                    this->previous_mode_ = this->kettle_durumu_;

                    this->kettle_durumu_ = KORUMA;
                    this->update_all_sensors();

                    if (this->previous_mode_ == NORMAL)
                    {
                        // LED durumlarını kaydet
                        bayled_previous_state = digitalRead(this->bay_led_pin_);
                        demled_previous_state = digitalRead(this->dem_led_pin_);

                        // DemLED'i kapat
                        if (digitalRead(this->dem_led_pin_) != LOW)
                        {
                            digitalWrite(this->dem_led_pin_, LOW);
                        }
                    }

                    // Tüm röleleri kapat
                    if (digitalRead(this->relay_pin_) != LOW)
                    {
                        digitalWrite(this->relay_pin_, LOW);
                    }
                    if (digitalRead(this->demleme_relay_pin_) != LOW)
                    {
                        digitalWrite(this->demleme_relay_pin_, LOW);
                    }
                    this->relay_active_ = false;
                    this->dem_relay_active_ = false;
                }
            }

            if (this->kettle_durumu_ == NORMAL)
            {
                this->check_water_level();
            }

            // kritik moddan çıkış için tuşları yine dinle
            if (this->kettle_durumu_ == NORMAL || this->kettle_durumu_ == KRITIK)
            {
                this->handle_touch_input();
            }

            // Sadece NORMAL durumda işlemlere devam et
            if (kettle_durumu_ == NORMAL)
            {
                // Tuşların genel durumunu kontrol et ve gerekirse sistemi sıfırla
                this->handle_global_state_reset();

                // Mama suyu işlemini kontrol et
                if (this->mama_suyu_durumu_ != MAMA_SUYU_KAPALI)
                {
                    this->handle_mama_suyu_hazirla();
                }

                // Su kaynatma işlemini kontrol et
                if (this->su_kaynatma_durumu_ != SU_KAYNATMA_KAPALI)
                {
                    this->handle_su_kaynatma();
                }

                // Çay demleme işlemini kontrol et
                if (this->cay_demleme_durumu_ != DEMLEME_KAPALI)
                {
                    this->handle_cay_demleme();
                }
            }
            
            if (this->kettle_durumu_ == NORMAL)
            {
                this->check_water_level();
            }

            if (this->kettle_durumu_ == KRITIK || this->previous_mode_ == KRITIK)
            {
                this->handle_critical_mode_leds(); // Kritik mod LED yanıp sönme
            }
            else if (this->kettle_durumu_ == KORUMA && this->current_mode_ != MODE_KAPALI && this->previous_mode_ == NORMAL)
            {
                this->handle_protection_mode_leds(); // Koruma mod LED yanıp sönme
            }

            this->handle_critical_sounds();

            this->process_demleme_feedback_();
        }
        void CayseverRobotea::handle_critical_sounds()
        {
            if (this->kritik_sound_active_ && this->kettle_durumu_ == KRITIK)
            {
                if (this->current_time_ - this->kritik_sound_start_time_ >= 1000)
                {
                    this->activate_sound(std::map<int, bool>{
                        {this->sound_pins_[0], true},
                        {this->sound_pins_[2], true},
                        {this->sound_pins_[1], false}});
                    this->kritik_sound_start_time_ = this->current_time_;
                }
            }
            else if (this->kettle_durumu_ != KRITIK && this->kritik_sound_active_)
            {
                this->kritik_sound_active_ = false;
            }
        }
        void CayseverRobotea::handle_protection_mode_leds()
        {
            static unsigned long last_blink_time = 0;
            static bool led_state = false;

            // Yanıp sönme kontrolü
            if (this->current_time_ - last_blink_time >= 1000) // 300ms yanıp sönme aralığı
            {
                last_blink_time = this->current_time_;
                led_state = !led_state;

                // BayLED'i yanıp söndür
                digitalWrite(this->bay_led_pin_, led_state ? HIGH : LOW);
            }
        }
        void CayseverRobotea::handle_critical_mode_leds()
        {
            static unsigned long last_blink_time = 0;
            static bool led_state = false;

            if (this->current_time_ - last_blink_time >= 300) // 300ms yanıp sönme aralığı
            {
                last_blink_time = this->current_time_;
                led_state = !led_state;

                // BayLED ve Tuş 1'in Beyaz LED'ini yanıp söndür
                if (led_state)
                {
                    if (digitalRead(this->bay_led_pin_) != HIGH)
                    {
                        digitalWrite(this->bay_led_pin_, HIGH);
                    }
                    this->control_led(0, true); // Tuş 1 Beyaz LED yan
                }
                else
                {
                    if (digitalRead(this->bay_led_pin_) != LOW)
                    {
                        digitalWrite(this->bay_led_pin_, LOW);
                    }
                    this->control_led(-1); // Tuş 1 Beyaz LED sön
                }
            }
        }

        void CayseverRobotea::led_blink(int pin, int times, int delay_ms)
        {
            for (int i = 0; i < times; i++)
            {
                digitalWrite(pin, HIGH); // LED'i aç
                delay(delay_ms);
                digitalWrite(pin, LOW); // LED'i kapat
                delay(delay_ms);
            }
        }

        void CayseverRobotea::on_wifi_connected()
        {
            ESP_LOGI("CayseverRobotea", "Wi-Fi bağlantısı sağlandı.");

            // Bay LED'i kapat
            if (digitalRead(this->bay_led_pin_) != LOW)
            {
                digitalWrite(this->bay_led_pin_, LOW);
            }

            // Dem LED'i 3 saniye boyunca yak
            digitalWrite(this->dem_led_pin_, HIGH);
            this->set_timeout("wifi_dem_led_off", 3000, [this]()
                              { digitalWrite(this->dem_led_pin_, LOW); });
        }

        void CayseverRobotea::on_wifi_disconnected()
        {
            ESP_LOGW("CayseverRobotea", "Wi-Fi bağlantısı başarısız.");

            // Bay LED'i açık bırak
            if (digitalRead(this->bay_led_pin_) != HIGH)
            {
                digitalWrite(this->bay_led_pin_, HIGH);
            }
        }

        void CayseverRobotea::handle_touch_input()
        {
            this->handle_touch_input_food_water();
            this->handle_touch_input_boiling_water();
            this->handle_touch_input_brew_tea();
        }

        void CayseverRobotea::handle_touch_input_food_water()
        {
            static unsigned long touch_start_time = 0;  // Tuş basılma başlangıç zamanı
            static unsigned long last_release_time = 0; // Son bırakma zamanı

            bool touch_value = digitalRead(this->touch_pins_[0]) == LOW;
            bool touch2 = digitalRead(this->touch_pins_[1]) == LOW;

            if (touch_value && !this->previous_touch_states_[0])
            {
                if (touch2)
                {
                    return;
                }
                touch_start_time = this->current_time_;
            }
            else if (!touch_value && this->previous_touch_states_[0])
            {
                if (touch2)
                {
                    return;
                }
                unsigned long press_duration = this->current_time_ - touch_start_time;
                if (press_duration >= 1200) // Tuş 1 1,2 sn basılı tutarsa kritik moddan çık
                {

                    if (this->kettle_durumu_ == KRITIK)
                    {

                        this->activate_sound(std::map<int, bool>{
                            {this->sound_pins_[0], true}, // GPIO4: HIGH
                            {this->sound_pins_[2], true}, // GPIO32: HIGH
                            {this->sound_pins_[1], false} // GPIO19: LOW
                        });

                        ESP_LOGI("CayseverRobotea", "Kritik moddan çıkılıyor. İşlemler devam ediyor.");
                        this->manual_exit = true;
                        this->kettle_durumu_ = NORMAL; // Durumu NORMAL'e döndür
                        this->update_all_sensors();

                        // LED'leri eski durumlarına döndür
                        digitalWrite(this->bay_led_pin_, bayled_previous_state);
                        digitalWrite(this->dem_led_pin_, demled_previous_state);
                        switch (this->current_mode_)
                        {
                        case MODE_SU_KAYNATMA:
                            if (this->su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA)
                            {
                                this->control_led(2, true);
                            }
                            else if (this->su_kaynatma_durumu_ == SU_KAYNATMA_HAZIRLIK)
                            {
                                this->control_led(2, false);
                            }

                            break;

                        case MODE_MAMA_SUYU:
                            if (this->mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA)
                            {
                                this->control_led(0, true);
                            }
                            else if (this->mama_suyu_durumu_ == MAMA_SUYU_HAZIRLIK)
                            {
                                this->control_led(0, false);
                            }
                            break;

                        case MODE_CAY_DEMLEME:
                            if (this->cay_demleme_durumu_ == DEMLEME_HAZIRLIK || this->cay_demleme_durumu_ == DEMLEME_BASLADI)
                            {
                                this->control_led(3, false);
                            }
                            else if (this->cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA)
                            {
                                this->control_led(3, true);
                            }
                            break;

                        case MODE_KAPALI:
                            this->control_led(-1);
                            break;
                        default:
                            break;
                        }
                    }
                }
                else if (kettle_durumu_ == NORMAL)
                {
                    this->play_button_sound();
                    this->update_mama_suyu(!this->touch_states_[0]);
                    last_release_time = this->current_time_;
                }
            }
            // Önceki durumu güncelle
            this->previous_touch_states_[0] = touch_value;
        }

        void CayseverRobotea::handle_touch_input_boiling_water()
        {
            if (kettle_durumu_ == KRITIK)
            {
                return;
            }
            // Dokunmatik pinin durumu
            bool touch_value = digitalRead(this->touch_pins_[2]) == LOW;
            bool touch2 = digitalRead(this->touch_pins_[3]) == LOW;

            if (touch_value && !this->previous_touch_states_[2])
            {
                if (touch2)
                {
                    return; // İki tuşa aynı anda basıldığında işlemi iptal et
                }
                this->play_button_sound();
                this->update_su_kaynatma(!this->touch_states_[2]);
            }

            // Önceki durumu güncelle
            this->previous_touch_states_[2] = touch_value;
        }
        void CayseverRobotea::handle_touch_input_brew_tea()
        {
            if (kettle_durumu_ == KRITIK)
            {
                return;
            }
            static unsigned long touch_start_time = 0;  // Tuş basılma başlangıç zamanı
            static unsigned long last_release_time = 0; // Son bırakma zamanı
            static int press_count = 0;                 // Bas çek sayacı

            // Dokunmatik pinin durumu
            bool touch_value = digitalRead(this->touch_pins_[3]) == LOW;
            bool touch2 = digitalRead(this->touch_pins_[2]) == LOW;

            if (touch_value && !this->previous_touch_states_[3])
            {
                if (touch2)
                {
                    return;
                }
                // Çay Demleme'e basıldı (ON)
                touch_start_time = this->current_time_;
                ESP_LOGI("CayseverRobotea", "Çay Demleme basıldı (ON).");
            }
            else if (!touch_value && this->previous_touch_states_[3])
            {
                if (touch2)
                {
                    return;
                }
                // Çay Demleme bırakıldı (KAPALI)
                unsigned long press_duration = this->current_time_ - touch_start_time;

                this->play_button_sound();

                if (this->current_mode_ == MODE_CAY_DEMLEME)
                {
                    ESP_LOGW("CayseverRobotea", "Çay Demleme: İşlem iptal ediliyor.");
                    this->set_mode(MODE_KAPALI, 0);
                    press_count = 0;
                }
                else
                {
                    // Dokunma işlemi algılandı
                    press_count++;
                    last_release_time = this->current_time_;
                    ESP_LOGI("CayseverRobotea", "Çay Demleme bırakıldı (KAPALI). Bas çek sayısı: %d", press_count);
                }
            }

            // Eğer bas çek işlemi tamamlandıysa (1000 ms'den uzun süre başka dokunma yok)
            if (this->current_time_ - last_release_time > 1000 && press_count > 0)
            {
                if (press_count > 4)
                {
                    // 4'ten fazla basılma durumunda dikkate alma
                    ESP_LOGW("CayseverRobotea", "Çay Demleme için maksimum 4 dokunma dikkate alınabilir. Dokunma sayısı sıfırlandı.");
                    press_count = 0;
                }
                else
                {
                    ESP_LOGI("CayseverRobotea", "Çay Demleme için toplam dokunma: %d", press_count);
                    this->set_mode(MODE_CAY_DEMLEME, press_count);
                    press_count = 0;
                }
            }

            // Önceki durumu güncelle
            this->previous_touch_states_[3] = touch_value;
        }

        void CayseverRobotea::handle_touch_input_toggle_button_sound()
        {
            static unsigned long touch_start_time = 0;              // Başlangıç zamanı
            static bool is_pressed = false;                         // Basılı durum
            bool touch1 = digitalRead(this->touch_pins_[0]) == LOW; // Tuş 1 durumu
            bool touch2 = digitalRead(this->touch_pins_[1]) == LOW; // Tuş 2 durumu

            if (touch1 && touch2)
            {
                if (!is_pressed)
                {
                    touch_start_time = this->current_time_; // İlk basılı zamanı kaydet
                    is_pressed = true;
                }
                else if (this->current_time_ - touch_start_time >= 2200)
                { // 3 saniye kontrolü
                    if (this->buton_sesi_switch_ != nullptr)
                    {
                        bool new_state = !this->buton_sesi_switch_->state;  // Durumu değiştir
                        this->buton_sesi_switch_->publish_state(new_state); // Yeni durumu yayınla
                        ESP_LOGI(TAG, "Buton sesi %s yapıldı.", new_state ? "aktif" : "pasif");

                        this->activate_sound(std::map<int, bool>{
                            {this->sound_pins_[0], true}, // GPIO4: HIGH
                            {this->sound_pins_[2], true}, // GPIO32: HIGH
                            {this->sound_pins_[1], false} // GPIO19: LOW
                        });
                    }
                    is_pressed = false; // İşlem tamamlandı, basılı durumu sıfırla
                }
            }
            else
            {
                is_pressed = false; // Her iki tuş da basılı değilse sıfırla
            }
        }

        void CayseverRobotea::handle_touch_input_toggle_speak_sound()
        {
            static unsigned long touch_start_time = 0;              // Başlangıç zamanı
            static bool is_pressed = false;                         // Basılı durum
            bool touch1 = digitalRead(this->touch_pins_[2]) == LOW; // Tuş 1 durumu
            bool touch2 = digitalRead(this->touch_pins_[3]) == LOW; // Tuş 2 durumu

            if (touch1 && touch2)
            {
                if (!is_pressed)
                {
                    touch_start_time = this->current_time_; // İlk basılı zamanı kaydet
                    is_pressed = true;
                }
                else if (this->current_time_ - touch_start_time >= 2200)
                { // 3 saniye kontrolü
                    if (this->konusma_sesi_switch_ != nullptr)
                    {
                        bool new_state = !this->konusma_sesi_switch_->state;  // Durumu değiştir
                        this->konusma_sesi_switch_->publish_state(new_state); // Yeni durumu yayınla
                        ESP_LOGI(TAG, "Konuşma sesi %s yapıldı.", new_state ? "aktif" : "pasif");

                        this->activate_sound(std::map<int, bool>{
                            {this->sound_pins_[0], true}, // GPIO4: HIGH
                            {this->sound_pins_[2], true}, // GPIO32: HIGH
                            {this->sound_pins_[1], false} // GPIO19: LOW
                        });
                    }
                    is_pressed = false; // İşlem tamamlandı, basılı durumu sıfırla
                }
            }
            else
            {
                is_pressed = false; // Her iki tuş da basılı değilse sıfırla
            }
        }

        void CayseverRobotea::visual_feedback_demleme_level(int level)
        {
            this->start_demleme_feedback_(level);
        }

        void CayseverRobotea::start_demleme_feedback_(int level)
        {
            if (level < 1)
                level = 1;
            if (level > 4)
                level = 4;

            this->demleme_fb_.active = true;
            this->demleme_fb_.level = level;
            this->demleme_fb_.blink_done = 0;
            this->demleme_fb_.phase_on = false;
            this->demleme_fb_.post_wait = false;
            this->demleme_fb_.next_ms = this->current_time_; // hemen başla
        }

        void CayseverRobotea::process_demleme_feedback_()
        {
            if (!this->demleme_fb_.active)
                return;

            // Demleme modunda değilsek feedback'i iptal et (başka moda geçilmiş olabilir)
            if (this->current_mode_ != MODE_CAY_DEMLEME || this->kettle_durumu_ != NORMAL)
            {
                this->demleme_fb_.active = false;
                return;
            }

            if (this->current_time_ < this->demleme_fb_.next_ms)
                return;

            // Blinkler bitti mi?
            if (this->demleme_fb_.blink_done >= this->demleme_fb_.level)
            {
                // 500ms bekle, sonra buton sesi çal ve bitir
                if (!this->demleme_fb_.post_wait)
                {
                    this->demleme_fb_.post_wait = true;
                    this->control_led(3); // sonunda kırmızı
                    this->demleme_fb_.next_ms = this->current_time_ + 500;
                    return;
                }

                // 500ms geçti -> ses + bitir
                this->play_button_sound();
                this->control_led(3); // kırmızı garanti
                this->demleme_fb_.active = false;
                ESP_LOGI("CayseverRobotea", "Demleme görsel geri bildirim tamamlandı: %d/4", this->demleme_fb_.level);
                return;
            }

            // Blink state machine: 300ms white ON, 300ms OFF
            if (!this->demleme_fb_.phase_on)
            {
                this->control_led(3, true); // white ON
                this->demleme_fb_.phase_on = true;
                this->demleme_fb_.next_ms = this->current_time_ + 300;
            }
            else
            {
                this->control_led(-1); // OFF
                this->demleme_fb_.phase_on = false;
                this->demleme_fb_.blink_done++;
                this->demleme_fb_.next_ms = this->current_time_ + 300;
            }
        }

        void CayseverRobotea::set_demleme_suresi_for_level_(int level)
        {
            // level: 1=MAX, 2=3/4, 3=2/4, 4=1/4
            switch (level)
            {
            case 1:
                this->demleme_suresi_ = 430;
                break;
            case 2:
                this->demleme_suresi_ = 330;
                break;
            case 3:
                this->demleme_suresi_ = 240;
                break;
            case 4:
                this->demleme_suresi_ = 150;
                break;
            default:
                this->demleme_suresi_ = 0;
                break;
            }
        }

        void CayseverRobotea::reset_all_operations(bool global_reset)
        {
            for (int j = 0; j < 4; j++)
            {
                if (this->touch_states_[j])
                {
                    this->touch_states_[j] = false;
                    ESP_LOGI("CayseverRobotea", "Tuş %d OFF yapıldı.", j + 1);
                }
            }
            this->cay_demleme_state_ = "KAPALI";

            // Su kaynatma, mama suyu ve çay demleme işlemlerini sıfırla
            this->mama_suyu_durumu_ = MAMA_SUYU_KAPALI;
            this->cay_demleme_durumu_ = DEMLEME_KAPALI;
            this->su_kaynatma_durumu_ = SU_KAYNATMA_KAPALI;
            this->update_all_sensors();

            // Tüm röleleri kapat
            if (digitalRead(this->relay_pin_) != LOW)
            {
                digitalWrite(this->relay_pin_, LOW);
            }
            if (digitalRead(this->demleme_relay_pin_) != LOW)
            {
                digitalWrite(this->demleme_relay_pin_, LOW);
            }
            this->relay_active_ = false;     // Röle durumu sıfırla
            this->dem_relay_active_ = false; // Demleme Röle durumu sıfırla

            // Tüm LED'leri kapat
            this->control_led(-1); // -1: tüm LED'leri kapat
            this->led_white_active_ = false;

            // DemLED ve BayLED sıfırlama
            if (digitalRead(this->dem_led_pin_) != LOW)
            {
                digitalWrite(this->dem_led_pin_, LOW);
            }
            if (digitalRead(this->bay_led_pin_) != LOW)
            {
                digitalWrite(this->bay_led_pin_, LOW);
            }
            this->demled_active_ = false; // DemLED durumunu sıfırla

            // Tüm tuş durumlarını sıfırla (global sıfırlamada)
            if (global_reset)
            {
                for (int i = 0; i < 4; i++)
                {
                    this->touch_states_[i] = false;
                    bool phys_pressed = (digitalRead(this->touch_pins_[i]) == LOW);
                    this->previous_touch_states_[i] = phys_pressed;
                }
            }

            ESP_LOGI("CayseverRobotea", "Tüm işlemler %s sıfırlandı.", global_reset ? "genel" : "lokal");
        }

        void CayseverRobotea::control_led(int button_index, bool is_white)
        {
            if (button_index == -1)
            {
                // Tüm LED'leri kapat
                for (int j = 0; j < 5; j++)
                {
                    digitalWrite(this->led_pins_[j], LOW);
                }
            }

            // Eğer geçerli bir tuş seçilmişse (button_index >= 0)
            if (button_index >= 0)
            {
                if (is_white)
                {
                    // Beyaz LED durumu
                    switch (button_index)
                    {
                    case 0:                                     // Tuş 1 beyaz
                        digitalWrite(this->led_pins_[0], LOW);  // GPIO15
                        digitalWrite(this->led_pins_[1], HIGH); // GPIO25
                        digitalWrite(this->led_pins_[2], HIGH); // GPIO13
                        digitalWrite(this->led_pins_[3], HIGH); // GPIO5
                        digitalWrite(this->led_pins_[4], HIGH); // GPIO26
                        break;
                    case 1: // Tuş 2 beyaz
                        digitalWrite(this->led_pins_[0], HIGH);
                        digitalWrite(this->led_pins_[1], HIGH);
                        digitalWrite(this->led_pins_[2], HIGH);
                        digitalWrite(this->led_pins_[3], LOW);
                        digitalWrite(this->led_pins_[4], HIGH);
                        break;
                    case 2: // Tuş 3 beyaz
                        digitalWrite(this->led_pins_[0], HIGH);
                        digitalWrite(this->led_pins_[1], HIGH);
                        digitalWrite(this->led_pins_[2], HIGH);
                        digitalWrite(this->led_pins_[3], LOW);
                        digitalWrite(this->led_pins_[4], LOW);
                        break;
                    case 3: // Tuş 4 beyaz
                        digitalWrite(this->led_pins_[0], HIGH);
                        digitalWrite(this->led_pins_[1], HIGH);
                        digitalWrite(this->led_pins_[2], LOW);
                        digitalWrite(this->led_pins_[3], LOW);
                        digitalWrite(this->led_pins_[4], LOW);
                        break;
                    }
                }
                else
                {
                    // Kırmızı LED durumu
                    switch (button_index)
                    {
                    case 0:                                     // Tuş 1 kırmızı
                        digitalWrite(this->led_pins_[0], HIGH); // GPIO15
                        digitalWrite(this->led_pins_[1], LOW);  // GPIO25
                        digitalWrite(this->led_pins_[2], LOW);  // GPIO13
                        digitalWrite(this->led_pins_[3], LOW);  // GPIO5
                        digitalWrite(this->led_pins_[4], LOW);  // GPIO26
                        break;
                    case 1: // Tuş 2 kırmızı
                        digitalWrite(this->led_pins_[0], LOW);
                        digitalWrite(this->led_pins_[1], LOW);
                        digitalWrite(this->led_pins_[2], LOW);
                        digitalWrite(this->led_pins_[3], HIGH);
                        digitalWrite(this->led_pins_[4], LOW);
                        break;
                    case 2: // Tuş 3 kırmızı
                        digitalWrite(this->led_pins_[0], LOW);
                        digitalWrite(this->led_pins_[1], LOW);
                        digitalWrite(this->led_pins_[2], LOW);
                        digitalWrite(this->led_pins_[3], HIGH);
                        digitalWrite(this->led_pins_[4], HIGH);
                        break;
                    case 3: // Tuş 4 kırmızı
                        digitalWrite(this->led_pins_[0], LOW);
                        digitalWrite(this->led_pins_[1], LOW);
                        digitalWrite(this->led_pins_[2], HIGH);
                        digitalWrite(this->led_pins_[3], HIGH);
                        digitalWrite(this->led_pins_[4], HIGH);
                        break;
                    }
                }
            }
        }

        void CayseverRobotea::play_button_sound()
        {
            if (!this->buton_sesi_switch_)
                return;

            if (this->buton_sesi_switch_->state)
            {
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[2], true}, // GPIO32: HIGH
                    {this->sound_pins_[1], false} // GPIO19: LOW
                });
            }
        }

        void CayseverRobotea::play_mama_suyu_hazir_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            {
                // Mama suyu sesi: GPIO4, GPIO19 ve GPIO32 HIGH
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[1], true}, // GPIO19: LOW
                    {this->sound_pins_[2], true}  // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::play_cay_demleme_start_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // Çay demleme sesi: GPIO4 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true},  // GPIO4: HIGH
                    {this->sound_pins_[1], false}, // GPIO19: LOW
                    {this->sound_pins_[2], false}  // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::play_cay_demleme_done_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // Çay demleme tamam sesi: GPIO4 ve GPIO19 HIGH, GPIO32 LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[1], true}, // GPIO19: LOW
                    {this->sound_pins_[2], false} // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::play_filtre_kahve_hazirlaniyor_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // Filtre kahve hazırlanıyor sesi: GPIO19 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], true},  // GPIO19: LOW
                    {this->sound_pins_[2], false}  // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::play_filtre_kahve_done_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // Filtre kahve tamam sesi: GPIO19 ve GPIO32 HIGH, GPIO4 LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], true},  // GPIO19: LOW
                    {this->sound_pins_[2], true}   // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::play_su_kaynadi_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // Su kaynadı sesi: GPIO32 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], false}, // GPIO19: LOW
                    {this->sound_pins_[2], true}   // GPIO32: HIGH
                });
            }
        }

        void CayseverRobotea::activate_sound(const std::map<int, bool> &pin_states)
        {
            // Pinleri set et
            for (const auto &entry : pin_states)
            {
                digitalWrite(entry.first, entry.second ? HIGH : LOW);
            }

            uint32_t token = ++this->sound_pulse_token_;
            this->set_timeout("sound_off", 10, [this, token]()
                              {
        // Aynı timeout ismi ile overwrite olacağı için genelde gerek yok ama güvenli kalsın
        if (token != this->sound_pulse_token_) return;

        for (int i = 0; i < 3; i++) {
            digitalWrite(this->sound_pins_[i], LOW);
        } });
        }

        void CayseverRobotea::handle_mama_suyu_hazirla()
        {
            if (kettle_durumu_ == KORUMA)
            {
                ESP_LOGW("CayseverRobotea", "Kettle koruma modunda. İşlem durduruldu.");
                // LED durumlarını kaydet
                bayled_previous_state = digitalRead(this->bay_led_pin_);
                demled_previous_state = digitalRead(this->dem_led_pin_);

                // DemLED'i kapat
                if (digitalRead(this->dem_led_pin_) != LOW)
                {
                    digitalWrite(this->dem_led_pin_, LOW);
                }

                return;
            }

            if (this->mama_suyu_durumu_ == MAMA_SUYU_KAPALI)
                return;

            // NTC sensöründen sıcaklık oku
            float temperature = this->ntc_sensor_->state;

            switch (this->mama_suyu_durumu_)
            {
            case MAMA_SUYU_HAZIRLIK:
                if (temperature >= 40.0f)
                {
                    // 40°C'ye ulaşıldığında döngüyü tamamla
                    if (digitalRead(this->relay_pin_) != LOW)
                    {
                        digitalWrite(this->relay_pin_, LOW);
                    }
                    this->relay_active_ = false;
                    this->mama_suyu_durumu_ = MAMA_SUYU_SICAKLIK_KORUMA;
                    this->update_all_sensors();

                    if (!this->led_white_active_)
                    {
                        this->control_led(0, true);         // Tuş 1’in beyaz LED’ini yak
                        this->led_white_active_ = true;     // Beyaz LED aktif duruma geçti
                        this->play_mama_suyu_hazir_sound(); // Mama suyu hazırlandı sesi
                    }

                    ESP_LOGI("CayseverRobotea", "Mama suyu hazırlama tamamlandı, sıcaklık koruma moduna geçildi.");
                }
                else if (temperature >= 33.0f)
                {
                    if (!this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle tekrar açıldı.", temperature);
                    }
                    else if (this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        if (digitalRead(this->relay_pin_) != LOW)
                        {
                            digitalWrite(this->relay_pin_, LOW);
                        }
                        this->relay_active_ = false;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle kapatıldı.", temperature);
                    }
                }
                else
                {
                    if (!this->relay_active_)
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle açıldı.", temperature);
                    }
                }
                break;

            case MAMA_SUYU_SICAKLIK_KORUMA:
                this->maintain_temperature(30.0f, 35.0f);

                if (temperature <= 30.0f)
                {
                    // Sıcaklık 30°C'nin altına düşerse röleyi tekrar aç
                    if (!this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle tekrar açıldı (koruma).", temperature);
                    }
                }
                else if (temperature >= 35.0f)
                {
                    // Sıcaklık 35°C'ye ulaştığında röleyi kapat
                    if (this->relay_active_)
                    {
                        if (digitalRead(this->relay_pin_) != LOW)
                        {
                            digitalWrite(this->relay_pin_, LOW);
                        }
                        this->relay_active_ = false;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle kapatıldı (koruma).", temperature);
                    }
                }
                break;

            default:
                break;
            }
        }

        void CayseverRobotea::handle_su_kaynatma()
        {
            if (kettle_durumu_ == KORUMA)
            {
                ESP_LOGW("CayseverRobotea", "Kettle koruma modunda. İşlem durduruldu.");
                return;
            }
            if (this->su_kaynatma_durumu_ == SU_KAYNATMA_KAPALI)
                return;

            float temperature = this->ntc_sensor_->state;

            switch (this->su_kaynatma_durumu_)
            {
            case SU_KAYNATMA_HAZIRLIK:
            {
                if (temperature >= OVERHEAT_CUTOFF_T)
                {
                    ESP_LOGE("CayseverRobotea", "OVERHEAT! T=%.2fC. Röle kapatiliyor, KRITIK.", temperature);

                    digitalWrite(this->relay_pin_, LOW);
                    digitalWrite(this->demleme_relay_pin_, LOW);
                    this->relay_active_ = false;
                    this->dem_relay_active_ = false;

                    this->kettle_durumu_ = KRITIK;
                    this->update_all_sensors();
                    return;
                }

                if (temperature >= 100.0f)
                {
                    // Eğer bu kaynatmada steam boost aktifse, kaynadıktan sonra bir süre daha güçlü buhar üret
                    if (this->steam_boost_enabled_)
                    {
                        if (this->su_kaynatma_boil_ms_ == 0)
                        {
                            this->su_kaynatma_boil_ms_ = this->current_time_;
                            ESP_LOGI("CayseverRobotea", "Kaynama goruldu. Steam boost basladi (%lus).",
                                     STEAM_BOOST_MS / 1000);
                        }

                        // boost süresi bitmediyse: 100..STEAM_BOOST_MAX_T aralığında tut
                        if (this->current_time_ - this->su_kaynatma_boil_ms_ < STEAM_BOOST_MS)
                        {
                            this->maintain_temperature(100.0f, STEAM_BOOST_MAX_T);

                            // güvenlik: boost sırasında max üstüne çıkarsa bekleme süresine takılmadan röleyi kapat
                            if (temperature >= STEAM_BOOST_MAX_T && this->relay_active_)
                            {
                                digitalWrite(this->relay_pin_, LOW);
                                this->relay_active_ = false;
                                this->last_relay_toggle_time_ = this->current_time_;
                            }

                            return; // boost devam ederken aşağıya düşme
                        }

                        // boost bitti -> bir daha boost yapma
                        this->steam_boost_enabled_ = false;
                        ESP_LOGI("CayseverRobotea", "Steam boost bitti, normal korumaya geciliyor.");
                    }

                    // Boost yoksa veya boost bittiyse: eski davranışın AYNISI
                    digitalWrite(this->relay_pin_, LOW);
                    this->relay_active_ = false;

                    this->su_kaynatma_durumu_ = SU_KAYNATMA_SICAKLIK_KORUMA;
                    this->update_all_sensors();

                    if (!this->led_white_active_)
                    {
                        this->control_led(2, true);
                        this->led_white_active_ = true;
                        this->play_su_kaynadi_sound();
                    }

                    ESP_LOGI("CayseverRobotea", "Su kaynama tamamlandi, SICAKLIK_KORUMA moduna gecildi.");
                }
                else if (temperature >= 93.0f)
                {
                    if (!this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle açıldı. Su Kaynatma", temperature);
                    }
                    else if (this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        if (digitalRead(this->relay_pin_) != LOW)
                        {
                            digitalWrite(this->relay_pin_, LOW);
                        }
                        this->relay_active_ = false;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle kapatıldı. Su Kaynatma", temperature);
                    }
                }
                else
                {
                    if (!this->relay_active_)
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle açıldı. Su Kaynatma", temperature);
                    }
                }
                break;
            }
            case SU_KAYNATMA_SICAKLIK_KORUMA:
            {
                this->maintain_temperature(95.0f, 99.0f);
                break;
            }
            default:
                break;
            }
        }

        void CayseverRobotea::handle_cay_demleme()
        {
            if (kettle_durumu_ == KORUMA)
            {
                ESP_LOGW("CayseverRobotea", "Kettle koruma modunda. İşlem durduruldu.");
                return;
            }

            if (this->cay_demleme_durumu_ == DEMLEME_KAPALI)
                return;

            // NTC sensöründen sıcaklık oku
            float temperature = this->ntc_sensor_->state;

            switch (this->cay_demleme_durumu_)
            {
            case DEMLEME_HAZIRLIK:
                // Kaynama döngüsü (93°C - 100°C)
                if (temperature >= 100.0f)
                {
                    // Sıcaklık 100°C'ye ulaştığında işlemi tamamla
                    if (digitalRead(this->relay_pin_) != LOW)
                    {
                        digitalWrite(this->relay_pin_, LOW);
                    }
                    this->relay_active_ = false;
                    this->cay_demleme_durumu_ = DEMLEME_BASLADI;
                    this->update_all_sensors();

                    this->demleme_start_time_ = this->current_time_; // Başlangıç zamanını kaydet
                    digitalWrite(this->demleme_relay_pin_, HIGH);    // Demleme rölesini aç
                    this->dem_relay_active_ = true;
                    ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Kaynama tamamlandı, çay demleme başladı.", temperature);

                    // LED güncellemesi ve sesli uyarı
                    this->control_led(3); // Tuş 4 kırmızı LED
                    this->play_cay_demleme_start_sound();
                }
                else if (temperature >= 93.0f)
                {
                    // 93°C ile 100°C arasında röleyi aç/kapat döngüsü
                    if (!this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle açıldı (kaynama devam ediyor).", temperature);
                    }
                    else if (this->relay_active_ && (this->current_time_ - this->last_relay_toggle_time_ >= this->relay_wait_time_))
                    {
                        if (digitalRead(this->relay_pin_) != LOW)
                        {
                            digitalWrite(this->relay_pin_, LOW);
                        }
                        this->relay_active_ = false;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle kapatıldı (kaynama devam ediyor).", temperature);
                    }
                }
                else
                {
                    // 93°C'nin altındaysa röleyi aç
                    if (!this->relay_active_)
                    {
                        digitalWrite(this->relay_pin_, HIGH); // Röleyi aç
                        this->relay_active_ = true;
                        this->last_relay_toggle_time_ = this->current_time_;
                        ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Röle açıldı (kaynama başlatılıyor).", temperature);
                    }
                }
                break;

            case DEMLEME_BASLADI:
                this->maintain_temperature(95.0f, 99.0f);

                // Demleme süresini kontrol et
                if (this->current_time_ - this->demleme_start_time_ >= this->demleme_suresi_ * 1000)
                {
                    if (this->dem_relay_active_)
                    {
                        // Demleme tamamlandığında röleyi kapat ve sıcaklık korumaya geç
                        if (digitalRead(this->demleme_relay_pin_) != LOW)
                        {
                            digitalWrite(this->demleme_relay_pin_, LOW);
                        }
                        this->dem_relay_active_ = false;

                        // 2 dakikalık dem alma süresini başlat
                        this->demleme_end_time_ = this->current_time_;
                    }
                    else if (this->current_time_ - this->demleme_end_time_ >= 240000)
                    {
                        this->cay_demleme_durumu_ = DEMLEME_SICAKLIK_KORUMA;
                        this->update_all_sensors();

                        ESP_LOGI("CayseverRobotea", "Çay demleme işlemi tamamlandı.");

                        // LED güncellemesi ve sesli uyarı
                        this->control_led(3, true);
                        if (digitalRead(this->dem_led_pin_) != HIGH)
                        {
                            digitalWrite(this->dem_led_pin_, HIGH);
                        }

                        this->play_cay_demleme_done_sound();
                        this->demled_start_time_ = this->current_time_;
                        this->demled_active_ = true;
                    }
                }

                break;

            case DEMLEME_SICAKLIK_KORUMA:
                this->maintain_temperature(95.0f, 99.0f);

                // DemLED süresini kontrol et
                if (this->demled_active_)
                {
                    unsigned long elapsed_time = this->current_time_ - this->demled_start_time_;
                    // 60 dakika = 3.600.000 ms
                    if (elapsed_time >= 3600000)
                    {
                        // tamamlandı, DemLED kapat ve BayLED'i aç
                        if (digitalRead(this->dem_led_pin_) != LOW)
                        {
                            digitalWrite(this->dem_led_pin_, LOW);
                        }
                        delay(10);
                        if (digitalRead(this->bay_led_pin_) != HIGH)
                        {
                            digitalWrite(this->bay_led_pin_, HIGH);
                        }
                        this->demled_active_ = false; // DemLED durumu sona erdi
                        ESP_LOGI("CayseverRobotea", "DemLED kapandı, BayLED aktif.");
                    }
                }
                break;

            default:
                break;
            }
        }

        void CayseverRobotea::maintain_temperature(float min, float max)
        {
            float t = this->ntc_sensor_->state;

            // Global overheat guard (HER MODDA)
            if (t >= OVERHEAT_CUTOFF_T)
            {
                ESP_LOGE("CayseverRobotea", "OVERHEAT! T=%.2fC. Röle kapatiliyor, KRITIK.", t);

                digitalWrite(this->relay_pin_, LOW);
                digitalWrite(this->demleme_relay_pin_, LOW);
                this->relay_active_ = false;
                this->dem_relay_active_ = false;

                this->kettle_durumu_ = KRITIK;
                this->update_all_sensors();
                return;
            }

            // minimum switch aralığı
            if (this->current_time_ - this->last_relay_toggle_time_ < this->relay_wait_time_)
                return;

            if (t <= min && !this->relay_active_)
            {
                digitalWrite(this->relay_pin_, HIGH);
                this->relay_active_ = true;
                this->last_relay_toggle_time_ = this->current_time_;
            }
            else if (t >= max && this->relay_active_)
            {
                digitalWrite(this->relay_pin_, LOW);
                this->relay_active_ = false;
                this->last_relay_toggle_time_ = this->current_time_;
            }
        }

        void CayseverRobotea::check_water_level()
        {
            if (!this->su_kontrol_switch_ || !this->su_kontrol_switch_->state)
                return;

            if (this->kettle_durumu_ != NORMAL)
                return;

            // Kettle yeni konduysa bir süre ölçme
            if (this->current_time_ < this->wl_grace_until_)
                return;

            if (!this->ntc_sensor_)
                return;

            const float t = this->ntc_sensor_->state;

            // Sert güvenlik (istersen 112..115 gibi daha aşağı da çekebiliriz)
            if (t >= 112.0f)
            {
                ESP_LOGE("CayseverRobotea", "WL HARD: T=%.2fC -> KRITIK", t);
                goto WL_CRITICAL;
            }

            // Hız ölçümü sadece röle ON iken ve 45..90 bandında anlamlı
            if (!this->relay_active_ || t < 45.0f || t > 90.0f)
            {
                this->wl_win_start_ms_ = 0;
                this->wl_win_start_t_ = 0;
                return;
            }

            constexpr uint32_t WIN_MS = 25000; // 25sn pencere
            constexpr float SLOPE_WARN = 0.40f;
            constexpr float SLOPE_HARD = 0.46f;
            constexpr uint8_t TRIP = 3;

            if (this->wl_win_start_ms_ == 0)
            {
                this->wl_win_start_ms_ = this->current_time_;
                this->wl_win_start_t_ = t;
                return;
            }

            uint32_t dt = this->current_time_ - this->wl_win_start_ms_;
            if (dt < WIN_MS)
                return;

            float slope = (t - this->wl_win_start_t_) / (dt / 1000.0f); // °C/s

            uint8_t add = 0;
            if (slope >= SLOPE_HARD)
                add = 2;
            else if (slope >= SLOPE_WARN)
                add = 1;

            if (add > 0)
            {
                this->wl_susp_ = (uint8_t)std::min<int>(TRIP, this->wl_susp_ + add);
                ESP_LOGW("CayseverRobotea", "WL rate: slope=%.2fC/s susp=%d", slope, this->wl_susp_);
            }
            else
            {
                if (this->wl_susp_ > 0)
                    this->wl_susp_--;
            }

            // pencereyi kaydır
            this->wl_win_start_ms_ = this->current_time_;
            this->wl_win_start_t_ = t;

            if (this->wl_susp_ >= TRIP)
            {
                ESP_LOGE("CayseverRobotea", "WL: su az olabilir (rate) -> KRITIK (T=%.2f)", t);
                goto WL_CRITICAL;
            }

            return;

        WL_CRITICAL:
            this->kettle_durumu_ = KRITIK;
            this->update_all_sensors();

            this->kritik_sound_start_time_ = this->current_time_;
            this->kritik_sound_active_ = true;

            digitalWrite(this->relay_pin_, LOW);
            digitalWrite(this->demleme_relay_pin_, LOW);
            this->relay_active_ = false;
            this->dem_relay_active_ = false;
        }

        void CayseverRobotea::handle_global_state_reset()
        {
            static bool already_reset = false;

            bool any_button_active = false;
            for (int i = 0; i < 4; i++)
            {
                if (this->touch_states_[i])
                {
                    any_button_active = true;
                    break;
                }
            }

            if (!any_button_active)
            {
                if (!already_reset)
                {
                    this->reset_all_operations(true);
                    ESP_LOGI("CayseverRobotea", "Tüm işlemler genel sıfırlandı.");
                    already_reset = true;
                }
            }
            else
            {
                already_reset = false;
            }
        }

        void CayseverRobotea::set_su_kaynatma_switch(switch_::Switch *su_kaynatma_switch)
        {
            this->su_kaynatma_switch_ = su_kaynatma_switch;
            this->su_kaynatma_switch_->add_on_state_callback([this](bool state)
                                                             {
            ESP_LOGI("CayseverRobotea", "on_su_kaynatma_change %s", state ? "true" : "false");
            this->on_su_kaynatma_change(state); });
        }
        void CayseverRobotea::set_mama_suyu_switch(switch_::Switch *mama_suyu_switch)
        {
            this->mama_suyu_switch_ = mama_suyu_switch;
            this->mama_suyu_switch_->add_on_state_callback([this](bool state)
                                                           {
            ESP_LOGI("CayseverRobotea", "on_mama_suyu_change %s", state ? "true" : "false");
            this->on_mama_suyu_change(state); });
        }
        void CayseverRobotea::update_su_kaynatma(bool su_kaynatma)
        {
            if (su_kaynatma)
            {
                this->set_mode(MODE_SU_KAYNATMA, 0);
            }
            else
            {
                this->set_mode(MODE_KAPALI, 0);
            }
        }

        void CayseverRobotea::update_mama_suyu(bool mama_suyu)
        {
            if (mama_suyu)
            {
                this->set_mode(MODE_MAMA_SUYU, 0);
            }
            else
            {
                this->set_mode(MODE_KAPALI, 0);
            }
        }
        const char *CayseverRobotea::active_mode_to_string(ActiveMode mode)
        {
            switch (mode)
            {
            case MODE_KAPALI:
                return "KAPALI";
            case MODE_SU_KAYNATMA:
                return "SU_KAYNATMA";
            case MODE_MAMA_SUYU:
                return "MAMA_SUYU";
            case MODE_CAY_DEMLEME:
                return "CAY_DEMLEME";
            }
            return "KAPALI";
        }
        void CayseverRobotea::publish_kettle_state_()
        {
            if (this->kettle_state_sensor_ != nullptr)
            {
                const char *state_str;
                switch (this->kettle_durumu_)
                {
                case NORMAL:
                    state_str = "NORMAL";
                    break;
                case KRITIK:
                    state_str = "KRITIK";
                    break;
                case KORUMA:
                    state_str = "KORUMA";
                    break;
                default:
                    state_str = "NORMAL";
                    break;
                }
                this->kettle_state_sensor_->publish_state(state_str);
            }
        }
        void CayseverRobotea::publish_mode_state_()
        {
            if (this->mode_state_sensor_ != nullptr)
            {
                const char *state_str;

                switch (this->current_mode_)
                {
                case MODE_KAPALI:
                {
                    state_str = "KAPALI";
                    break;
                }
                case MODE_SU_KAYNATMA:
                {
                    switch (this->su_kaynatma_durumu_)
                    {
                    case SU_KAYNATMA_HAZIRLIK:
                        state_str = "HAZIRLIK";
                        break;
                    case SU_KAYNATMA_SICAKLIK_KORUMA:
                        state_str = "SICAKLIK_KORUMA";
                        break;
                    case SU_KAYNATMA_KAPALI:
                        state_str = "KAPALI";
                        break;
                    }
                    break;
                }
                case MODE_MAMA_SUYU:
                {
                    switch (this->mama_suyu_durumu_)
                    {
                    case MAMA_SUYU_HAZIRLIK:
                        state_str = "HAZIRLIK";
                        break;
                    case MAMA_SUYU_SICAKLIK_KORUMA:
                        state_str = "SICAKLIK_KORUMA";
                        break;
                    case MAMA_SUYU_KAPALI:
                        state_str = "KAPALI";
                        break;
                    }
                    break;
                }
                case MODE_CAY_DEMLEME:
                {
                    switch (this->cay_demleme_durumu_)
                    {
                    case DEMLEME_BASLADI:
                        state_str = "DEMLEME_BASLADI";
                        break;
                    case DEMLEME_HAZIRLIK:
                        state_str = "HAZIRLIK";
                        break;
                    case DEMLEME_SICAKLIK_KORUMA:
                        state_str = "SICAKLIK_KORUMA";
                        break;
                    case DEMLEME_KAPALI:
                        state_str = "KAPALI";
                        break;
                    }
                    break;
                }

                default:
                    state_str = "KAPALI";
                    break;
                }
                this->mode_state_sensor_->publish_state(state_str);
            }
        }
        void CayseverRobotea::publish_mode_()
        {
            if (this->mode_sensor_ != nullptr)
            {
                this->mode_sensor_->publish_state(this->active_mode_to_string(this->current_mode_));
            }
        }

        void CayseverRobotea::update_all_sensors()
        {
            static ActiveMode last_mode = MODE_KAPALI;

            if (this->current_mode_ != last_mode)
            {
                this->publish_mode_();
                last_mode = this->current_mode_;
            }

            this->publish_kettle_state_();
            this->publish_mode_state_();
        }

        void CayseverRobotea::set_mode(ActiveMode new_mode, int press_count)
        {
            this->pending_mode_ = new_mode;
            this->pending_press_count_ = press_count;
            this->pending_mode_change_ = true;
            this->schedule_process_pending_();
        }

        void CayseverRobotea::apply_mode_(ActiveMode new_mode, int press_count)
        {
            ESP_LOGI("CayseverRobotea", "set_mode: Yeni mod => %d", new_mode);

            auto publish_demleme_switch = [&](bool st)
            {
                if (this->cay_demleme_max_switch_ != nullptr)
                {
                    this->suppress_cay_demleme_max_cb_ = true;
                    this->cay_demleme_max_switch_->publish_state(st);
                    this->suppress_cay_demleme_max_cb_ = false;
                }
            };

            // 1) Eski modu kapat
            switch (this->current_mode_)
            {
            case MODE_SU_KAYNATMA:
                this->reset_all_operations(false);
                if (this->su_kaynatma_switch_)
                    this->su_kaynatma_switch_->publish_state(false);
                break;

            case MODE_MAMA_SUYU:
                this->reset_all_operations(false);
                if (this->mama_suyu_switch_)
                    this->mama_suyu_switch_->publish_state(false);
                break;

            case MODE_CAY_DEMLEME:
                this->reset_all_operations(false);
                if (this->current_mode_ != new_mode)
                {
                    if (this->cay_demleme_select_->current_option() != "KAPALI")
                        this->cay_demleme_select_->publish_state("KAPALI");

                    publish_demleme_switch(false);
                }
                break;

            case MODE_KAPALI:
                this->reset_all_operations(false);
                publish_demleme_switch(false);
                break;
            default:
                // Zaten hiçbir mod aktif değil
                break;
            }

            // 2) Yeni modu ayarla
            this->current_mode_ = new_mode;

            // 3) Yeni mod ON işlemleri
            switch (new_mode)
            {
            case MODE_KAPALI:
                this->reset_all_operations(false);
                publish_demleme_switch(false);
                break;

            case MODE_SU_KAYNATMA:
            {
                this->touch_states_[2] = true;
                this->control_led(2);
                this->su_kaynatma_durumu_ = SU_KAYNATMA_HAZIRLIK;

                this->su_kaynatma_boil_ms_ = 0; // bu kaynatma için kaynama anı daha görülmedi
                float start_t = (this->ntc_sensor_ ? this->ntc_sensor_->state : 999.0f);
                this->steam_boost_enabled_ = (start_t < STEAM_BOOST_START_T);

                ESP_LOGI("CayseverRobotea", "SteamBoost=%s (start_t=%.2f)",
                         this->steam_boost_enabled_ ? "AKTIF" : "PASIF", start_t);

                if (this->su_kaynatma_switch_)
                    this->su_kaynatma_switch_->publish_state(true);

                break;
            }
            case MODE_MAMA_SUYU:
                this->touch_states_[0] = true;
                this->control_led(0);
                this->mama_suyu_durumu_ = MAMA_SUYU_HAZIRLIK;
                if (this->mama_suyu_switch_)
                    this->mama_suyu_switch_->publish_state(true);

                break;

            case MODE_CAY_DEMLEME:
                this->touch_states_[3] = true;

                int level = press_count;
                if (level < 1)
                    level = 1;
                if (level > 4)
                    level = 4;

                // Süreyi burada ayarla (artık feedback fonksiyonu süre set etmiyor)
                this->set_demleme_suresi_for_level_(level);

                // Non-blocking görsel feedback başlat
                this->visual_feedback_demleme_level(level);

                ESP_LOGI("CayseverRobotea", "Çay demleme işlemi başlıyor. Süre: %d saniye.", this->demleme_suresi_);
                this->cay_demleme_durumu_ = DEMLEME_HAZIRLIK;

                // Select nesnesini güncelle (MAX/3/4/2/4/1/4)
                if (this->cay_demleme_select_ != nullptr)
                {
                    std::string new_state = "1/4";
                    if (level == 1)
                        new_state = "MAX";
                    else if (level == 2)
                        new_state = "3/4";
                    else if (level == 3)
                        new_state = "2/4";

                    this->cay_demleme_state_ = new_state;
                    this->cay_demleme_select_->publish_state(new_state);
                }

                // SADECE MAX switch (level==1) ON olsun
                publish_demleme_switch(level == 1);
                break;
            }

            // 4) Mod sensoru yayınla
            this->update_all_sensors();
        }

        void CayseverRobotea::schedule_process_pending_()
        {
            if (this->pending_process_scheduled_)
                return;
            this->pending_process_scheduled_ = true;

            this->set_timeout("process_pending", 0, [this]()
                              { this->process_pending_(); });
        }

        void CayseverRobotea::process_pending_()
        {
            this->pending_process_scheduled_ = false;

            if (!this->pending_mode_change_)
                return;
            this->pending_mode_change_ = false;

            this->apply_mode_(this->pending_mode_, this->pending_press_count_);
        }

        void CayseverRobotea::set_cay_demleme_select(select::Select *cay_demleme_select)
        {
            this->cay_demleme_select_ = cay_demleme_select;
            this->cay_demleme_select_->publish_state("KAPALI");

            this->cay_demleme_select_->add_on_state_callback([this](size_t index)
                                                             {
        (void)index;
        std::string opt = this->cay_demleme_select_->current_option();
        ESP_LOGI("CayseverRobotea", "on_cay_demleme_change %s", opt.c_str());
        this->on_cay_demleme_change(opt); });
        }

        void CayseverRobotea::update_cay_demleme(const std::string &level)
        {
            this->cay_demleme_state_ = level;

            int numeric_level = 0;
            bool level_state = false;
            if (level == "1/4")
            {
                level_state = true;
                numeric_level = 4;
            }
            else if (level == "2/4")
            {
                level_state = true;
                numeric_level = 3;
            }
            else if (level == "3/4")
            {
                level_state = true;
                numeric_level = 2;
            }
            else if (level == "MAX")
            {
                level_state = true;
                numeric_level = 1;
            }
            else if (level == "KAPALI")
            {
                level_state = false;
                numeric_level = 0;
            }
            else
            {
                ESP_LOGW("CayseverRobotea", "Geçersiz çay demleme seviyesi: %s", level.c_str());
                level_state = false;
                numeric_level = 0;
            }

            if (level_state)
            {
                this->set_mode(MODE_CAY_DEMLEME, numeric_level);
            }
            else
            {
                this->set_mode(MODE_KAPALI, 0);
            }
        }

        void CayseverRobotea::set_cay_demleme_max_switch(switch_::Switch *sw)
        {
            this->cay_demleme_max_switch_ = sw;

            if (this->cay_demleme_max_switch_ != nullptr)
            {
                // başlangıçta OFF (callback tetiklemesin diye suppress)
                this->suppress_cay_demleme_max_cb_ = true;
                this->cay_demleme_max_switch_->publish_state(false);
                this->suppress_cay_demleme_max_cb_ = false;

                this->cay_demleme_max_switch_->add_on_state_callback([this](bool state)
                                                                     { this->on_cay_demleme_max_change(state); });
            }
        }

        void CayseverRobotea::set_buton_sesi_switch(switch_::Switch *buton_sesi_switch)
        {
            this->buton_sesi_switch_ = buton_sesi_switch;

            if (this->buton_sesi_switch_ != nullptr)
            {
                ESP_LOGI("CayseverRobotea", "Buton sesi switch başarıyla ayarlandı.");
                this->buton_sesi_switch_->add_on_state_callback([this](bool state)
                                                                { ESP_LOGI("CayseverRobotea", "Buton sesi switch durumu değişti: %s", state ? "ON" : "OFF"); });
            }
            else
            {
                ESP_LOGW("CayseverRobotea", "Buton sesi switch NULL!");
            }
        }

        void CayseverRobotea::set_konusma_sesi_switch(switch_::Switch *konusma_sesi_switch)
        {
            this->konusma_sesi_switch_ = konusma_sesi_switch;

            if (this->konusma_sesi_switch_ != nullptr)
            {
                ESP_LOGI("CayseverRobotea", "Konuşma sesi switch başarıyla ayarlandı.");
                this->konusma_sesi_switch_->add_on_state_callback([this](bool state)
                                                                  { ESP_LOGI("CayseverRobotea", "Konuşma sesi switch durumu değişti: %s", state ? "ON" : "OFF"); });
            }
        }

        void CayseverRobotea::set_su_kontrol_switch(switch_::Switch *su_kontrol_switch)
        {
            this->su_kontrol_switch_ = su_kontrol_switch;

            if (this->su_kontrol_switch_ != nullptr)
            {
                ESP_LOGI("CayseverRobotea", "Su kontrol switch başarıyla ayarlandı.");
                this->su_kontrol_switch_->add_on_state_callback([this](bool state)
                                                                { ESP_LOGI("CayseverRobotea", "Su kontrol switch durumu değişti: %s", state ? "ON" : "OFF"); });
            }
        }

    } // namespace caysever_robotea
} // namespace esphome
