#include "caysever_robotea.h"
#include "esphome/core/log.h"
#include <WiFi.h>

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
            digitalWrite(this->relay_pin_, LOW); // Röleyi başlangıçta kapalı yap

            // Demleme rölesi pinini çıkış olarak ayarla ve başlangıçta kapalı yap
            pinMode(this->demleme_relay_pin_, OUTPUT);
            digitalWrite(this->demleme_relay_pin_, LOW);

            // Ses pinlerini çıkış olarak ayarla ve başlangıç durumunu LOW yap
            for (int i = 0; i < 3; i++) // Burada `sound_pins_` 3 elemanlı bir dizi
            {
                pinMode(this->sound_pins_[i], OUTPUT);
                digitalWrite(this->sound_pins_[i], LOW);
            }
            this->current_mode_ = MODE_NONE;
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
        }

        // loop metodu tanımı
        void CayseverRobotea::loop()
        {
            this->current_time_ = millis();
            // Sensör değerlerini kontrol edin
            if (this->ntc_sensor_)
            {
                float temperature = this->ntc_sensor_->state;

                if (temperature < 0.0f) // Anormal sıcaklık değeri
                {
                    if (kettle_durumu_ == NORMAL)
                    {
                        ESP_LOGW("CayseverRobotea", "Kettle kaldırıldı veya NTC sensör hatası. Koruma moduna geçiliyor.");
                        kettle_durumu_ = KORUMA;

                        // Tüm röleleri kapat
                        digitalWrite(this->relay_pin_, LOW);
                        digitalWrite(this->demleme_relay_pin_, LOW);
                        this->relay_active_ = false;
                        this->dem_relay_active_ = false;
                    }
                }
                else
                {
                    if (kettle_durumu_ == KORUMA)
                    {
                        ESP_LOGI("CayseverRobotea", "Kettle yerine yerleştirildi. İşlemler devam ediyor.");
                        kettle_durumu_ = NORMAL;
                    }
                }
            }
            else
            {
                if (kettle_durumu_ == NORMAL)
                {
                    ESP_LOGW("CayseverRobotea", "NTC sensörü bağlı değil!");
                    kettle_durumu_ = KORUMA;

                    // Tüm röleleri kapat
                    digitalWrite(this->relay_pin_, LOW);
                    digitalWrite(this->demleme_relay_pin_, LOW);
                    this->relay_active_ = false;
                    this->dem_relay_active_ = false;
                }
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

                // Dokunmatik girişlerini yönet
                this->handle_touch_input();
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
            digitalWrite(this->bay_led_pin_, LOW);

            // Dem LED'i 3 saniye boyunca yak
            digitalWrite(this->dem_led_pin_, HIGH);
            delay(3000);
            digitalWrite(this->dem_led_pin_, LOW);
        }

        void CayseverRobotea::on_wifi_disconnected()
        {
            ESP_LOGW("CayseverRobotea", "Wi-Fi bağlantısı başarısız.");

            // Bay LED'i açık bırak
            digitalWrite(this->bay_led_pin_, HIGH);
        }

        void CayseverRobotea::handle_touch_input()
        {
            this->handle_touch_input_food_water();
            this->handle_touch_input_boiling_water();
            this->handle_touch_input_brew_tea();
        }
        void CayseverRobotea::handle_touch_input_food_water()
        {
            // Dokunmatik pinin durumu
            bool touch_value = digitalRead(this->touch_pins_[0]) == LOW;

            if (touch_value && !this->previous_touch_states_[0])
            {
                this->play_button_sound();
                this->update_mama_suyu(!this->touch_states_[0]);
            }
            // Önceki durumu güncelle
            this->previous_touch_states_[0] = touch_value;
        }
        void CayseverRobotea::handle_touch_input_boiling_water()
        {
            // Dokunmatik pinin durumu
            bool touch_value = digitalRead(this->touch_pins_[2]) == LOW;

            if (touch_value && !this->previous_touch_states_[2])
            {
                this->play_button_sound();
                this->update_su_kaynatma(!this->touch_states_[2]);
            }

            // Önceki durumu güncelle
            this->previous_touch_states_[2] = touch_value;
        }
        void CayseverRobotea::handle_touch_input_brew_tea()
        {
            static unsigned long touch_start_time = 0;  // Tuş basılma başlangıç zamanı
            static unsigned long last_release_time = 0; // Son bırakma zamanı
            static int press_count = 0;                 // Bas çek sayacı

            // Dokunmatik pinin durumu
            bool touch_value = digitalRead(this->touch_pins_[3]) == LOW;

            if (touch_value && !this->previous_touch_states_[3])
            {
                // Çay Demleme'e basıldı (ON)
                touch_start_time = this->current_time_;
                ESP_LOGI("CayseverRobotea", "Çay Demleme basıldı (ON).");
            }
            else if (!touch_value && this->previous_touch_states_[3])
            {
                // Çay Demleme bırakıldı (OFF)
                unsigned long press_duration = this->current_time_ - touch_start_time;

                if (press_duration >= 1500)
                {
                    // 1,5 saniyeden fazla basılı tutuldu, işlem iptal ediliyor
                    ESP_LOGW("CayseverRobotea", "Çay Demleme: İşlem iptal edildi (1,5 saniye basılı tutuldu).");
                    this->set_mode(MODE_NONE, 0);
                    press_count = 0;
                }
                else
                {
                    this->play_button_sound();
                    // Dokunma işlemi algılandı
                    press_count++;
                    last_release_time = this->current_time_;
                    ESP_LOGI("CayseverRobotea", "Çay Demleme bırakıldı (OFF). Bas çek sayısı: %d", press_count);
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
        void CayseverRobotea::visual_feedback_demleme_level(int level)
        {
            for (int i = 0; i < level; i++)
            {
                this->control_led(3, true); // Beyaz LED yan
                delay(300);
                this->control_led(-1); // Beyaz LED sön
                delay(300);
            }
            delay(500);
            this->play_button_sound();
            // Son olarak kırmızı LED yanar
            this->control_led(3);

            // Demleme süresini belirle
            switch (level)
            {
            case 1:
                this->demleme_suresi_ = 120; // 2 dakika
                break;
            case 2:
                this->demleme_suresi_ = 210; // 3 dakika 30 saniye
                break;
            case 3:
                this->demleme_suresi_ = 300; // 5 dakika
                break;
            case 4:
                this->demleme_suresi_ = 420; // 7 dakika
                break;
            default:
                this->demleme_suresi_ = 0;
                break;
            }

            ESP_LOGI("CayseverRobotea", "Tuş 4 için görsel geri bildirim tamamlandı: %d/4 seviyesinde ayarlandı.", level);
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
            this->cay_demleme_state_ = "OFF";

            // Su kaynatma, mama suyu ve çay demleme işlemlerini sıfırla
            this->mama_suyu_durumu_ = MAMA_SUYU_KAPALI;
            this->cay_demleme_durumu_ = DEMLEME_KAPALI;
            this->su_kaynatma_durumu_ = SU_KAYNATMA_KAPALI;

            // Tüm röleleri kapat
            digitalWrite(this->relay_pin_, LOW);         // Su kaynatma rölesini kapat
            digitalWrite(this->demleme_relay_pin_, LOW); // Demleme rölesini kapat
            this->relay_active_ = false;                 // Röle durumu sıfırla
            this->dem_relay_active_ = false;             // Demleme Röle durumu sıfırla

            // Tüm LED'leri kapat
            this->control_led(-1); // -1: tüm LED'leri kapat
            this->led_white_active_ = false;

            // DemLED ve BayLED sıfırlama
            digitalWrite(this->dem_led_pin_, LOW); // DemLED'i kapat
            digitalWrite(this->bay_led_pin_, LOW); // BayLED'i kapat
            this->demled_active_ = false;          // DemLED durumunu sıfırla

            // Tüm tuş durumlarını sıfırla (global sıfırlamada)
            if (global_reset)
            {
                for (int i = 0; i < 4; i++)
                {
                    this->touch_states_[i] = false;
                    this->previous_touch_states_[i] = false;
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
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], true}, // GPIO4: HIGH
                {this->sound_pins_[2], true}, // GPIO32: HIGH
                {this->sound_pins_[1], false} // GPIO19: LOW
            });
        }

        void CayseverRobotea::play_mama_suyu_hazir_sound()
        {

            // Mama suyu sesi: GPIO4, GPIO19 ve GPIO32 HIGH
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], true}, // GPIO4: HIGH
                {this->sound_pins_[1], true}, // GPIO19: LOW
                {this->sound_pins_[2], true}  // GPIO32: HIGH
            });
        }

        void CayseverRobotea::play_cay_demleme_start_sound()
        {
            // Çay demleme sesi: GPIO4 HIGH, diğerleri LOW
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], true},  // GPIO4: HIGH
                {this->sound_pins_[1], false}, // GPIO19: LOW
                {this->sound_pins_[2], false}  // GPIO32: HIGH
            });
        }

        void CayseverRobotea::play_cay_demleme_done_sound()
        {
            // Çay demleme tamam sesi: GPIO4 ve GPIO19 HIGH, GPIO32 LOW
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], true}, // GPIO4: HIGH
                {this->sound_pins_[1], true}, // GPIO19: LOW
                {this->sound_pins_[2], false} // GPIO32: HIGH
            });
        }

        void CayseverRobotea::play_filtre_kahve_hazirlaniyor_sound()
        {
            // Filtre kahve hazırlanıyor sesi: GPIO19 HIGH, diğerleri LOW
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], false}, // GPIO4: HIGH
                {this->sound_pins_[1], true},  // GPIO19: LOW
                {this->sound_pins_[2], false}  // GPIO32: HIGH
            });
        }

        void CayseverRobotea::play_filtre_kahve_done_sound()
        {
            // Filtre kahve tamam sesi: GPIO19 ve GPIO32 HIGH, GPIO4 LOW
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], false}, // GPIO4: HIGH
                {this->sound_pins_[1], true},  // GPIO19: LOW
                {this->sound_pins_[2], true}   // GPIO32: HIGH
            });
        }

        void CayseverRobotea::play_su_kaynadi_sound()
        {
            // Su kaynadı sesi: GPIO32 HIGH, diğerleri LOW
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], false}, // GPIO4: HIGH
                {this->sound_pins_[1], false}, // GPIO19: LOW
                {this->sound_pins_[2], true}   // GPIO32: HIGH
            });
        }

        void CayseverRobotea::activate_sound(const std::map<int, bool> &pin_states)
        {
            // Tüm pinlerin durumlarını ayarla
            for (const auto &entry : pin_states)
            {
                int pin = entry.first;
                bool state = entry.second;
                digitalWrite(pin, state ? HIGH : LOW);
            }

            // Kısa bir gecikme
            delay(10);

            // Tüm pinleri LOW duruma getir
            for (const auto &entry : pin_states)
            {
                int pin = entry.first;
                digitalWrite(pin, LOW);
            }
        }

        void CayseverRobotea::handle_mama_suyu_hazirla()
        {
            if (kettle_durumu_ == KORUMA)
            {
                ESP_LOGW("CayseverRobotea", "Kettle koruma modunda. İşlem durduruldu.");
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
                    digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
                    this->relay_active_ = false;
                    this->mama_suyu_durumu_ = MAMA_SUYU_SICAKLIK_KORUMA;
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
                        digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
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
                        digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
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
                if (temperature >= 100.0f)
                {
                    digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
                    this->relay_active_ = false;
                    this->su_kaynatma_durumu_ = SU_KAYNATMA_SICAKLIK_KORUMA;
                    // Beyaz LED'in zaten yakıldığını kontrol edin
                    if (!this->led_white_active_)
                    {
                        this->control_led(2, true);     // Tuş 3’ün beyaz LED’ini yak
                        this->led_white_active_ = true; // Beyaz LED aktif duruma geçti
                        this->play_su_kaynadi_sound();  // Su kaynadı sesi
                    }

                    ESP_LOGI("CayseverRobotea", "Su kaynama tamamlandı, koruma moduna geçildi.");
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
                        digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
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
                this->maintain_temperature(90.0f, 95.0f);
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
                    digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
                    this->relay_active_ = false;
                    this->cay_demleme_durumu_ = DEMLEME_BASLADI;
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
                        digitalWrite(this->relay_pin_, LOW); // Röleyi kapat
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
                // Koruma Modu (90°C - 95°C)
                this->maintain_temperature(90.0f, 95.0f);

                // Demleme süresini kontrol et
                if (this->current_time_ - this->demleme_start_time_ >= this->demleme_suresi_ * 1000)
                {
                    if (this->dem_relay_active_)
                    {
                        // Demleme tamamlandığında röleyi kapat ve sıcaklık korumaya geç
                        digitalWrite(this->demleme_relay_pin_, LOW); // Demleme rölesini kapat
                        this->dem_relay_active_ = false;

                        // 2 dakikalık dem alma süresini başlat
                        this->demleme_end_time_ = this->current_time_;
                    }
                    else if (this->current_time_ - this->demleme_end_time_ >= 120000)
                    {
                        this->cay_demleme_durumu_ = DEMLEME_SICAKLIK_KORUMA;

                        ESP_LOGI("CayseverRobotea", "Çay demleme işlemi tamamlandı.");

                        // LED güncellemesi ve sesli uyarı
                        this->control_led(3, true);             // Tuş 4 beyaz LED
                        digitalWrite(this->dem_led_pin_, HIGH); // DemLED aktif
                        this->play_cay_demleme_done_sound();
                        this->demled_start_time_ = this->current_time_; // DemLED başlangıç zamanını kaydet
                        this->demled_active_ = true;
                    }
                }

                break;

            case DEMLEME_SICAKLIK_KORUMA:
                // Koruma Modu (90°C - 95°C)
                this->maintain_temperature(90.0f, 95.0f);

                // DemLED süresini kontrol et
                if (this->demled_active_)
                {
                    unsigned long elapsed_time = this->current_time_ - this->demled_start_time_;
                    // 60 dakika = 3.600.000 ms
                    if (elapsed_time >= 3600000)
                    {
                        // 1 dakika tamamlandı, DemLED kapat ve BayLED'i aç
                        digitalWrite(this->dem_led_pin_, LOW); // DemLED pasif
                        delay(10);
                        digitalWrite(this->bay_led_pin_, HIGH); // BayLED aktif
                        this->demled_active_ = false;           // DemLED durumu sona erdi
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
            float temperature = this->ntc_sensor_->state;

            // Sıcaklık koruma modunda röleyi aç/kapat döngüsü
            if (temperature <= min)
            {
                if (!this->relay_active_)
                {
                    digitalWrite(this->relay_pin_, HIGH);
                    this->relay_active_ = true;
                    this->last_relay_toggle_time_ = this->current_time_;
                    ESP_LOGI("CayseverRobotea", "Sıcaklık koruma: %.2f°C, Röle tekrar açıldı.", temperature);
                }
            }
            else if (temperature >= max)
            {
                if (this->relay_active_)
                {
                    digitalWrite(this->relay_pin_, LOW);
                    this->relay_active_ = false;
                    this->last_relay_toggle_time_ = this->current_time_;
                    ESP_LOGI("CayseverRobotea", "Sıcaklık koruma: %.2f°C, Röle kapatıldı.", temperature);
                }
            }
        }

        void CayseverRobotea::handle_global_state_reset()
        {
            bool any_button_active = false;

            // Herhangi bir tuşun aktif olup olmadığını kontrol et
            for (int i = 0; i < 4; i++)
            {
                if (this->touch_states_[i])
                {
                    any_button_active = true;
                    break;
                }
            }

            // Eğer hiçbir tuş aktif değilse işlemleri sıfırla
            if (!any_button_active)
            {
                static bool previous_reset_state = false;

                // Eğer zaten sıfırlanmışsa tekrar sıfırlama yapma
                if (!previous_reset_state)
                {
                    this->reset_all_operations(true); // Genel sıfırlama
                    ESP_LOGI("CayseverRobotea", "Tüm işlemler genel sıfırlandı.");
                    previous_reset_state = true;
                }
            }
            else
            {
                // Bir tuş aktif olduğunda sıfırlama durumunu temizle
                static bool previous_reset_state = false;
                previous_reset_state = false;
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
                this->set_mode(MODE_NONE, 0);
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
                this->set_mode(MODE_NONE, 0);
            }
        }
        const char *CayseverRobotea::active_mode_to_string(ActiveMode mode)
        {
            switch (mode)
            {
            case MODE_NONE:
                return "NONE";
            case MODE_SU_KAYNATMA:
                return "SU_KAYNATMA";
            case MODE_MAMA_SUYU:
                return "MAMA_SUYU";
            case MODE_CAY_DEMLEME:
                return "CAY_DEMLEME";
            }
            return "NONE";
        }
        void CayseverRobotea::publish_mode_()
        {
            if (this->mode_sensor_ != nullptr)
            {
                this->mode_sensor_->publish_state(this->active_mode_to_string(this->current_mode_));
            }
        }
        void CayseverRobotea::set_mode(ActiveMode new_mode, int press_count)
        {
            ESP_LOGI("CayseverRobotea", "set_mode: Yeni mod => %d", new_mode);

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
                    if (this->cay_demleme_select_->state != "OFF")
                    {
                        this->cay_demleme_select_->publish_state("OFF");
                    }
                }
                break;

            case MODE_NONE:
                this->reset_all_operations(false);
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
            case MODE_NONE:
                this->reset_all_operations(false);
                break;

            case MODE_SU_KAYNATMA:
                this->touch_states_[2] = true;
                this->control_led(2);
                this->su_kaynatma_durumu_ = SU_KAYNATMA_HAZIRLIK;
                if (this->su_kaynatma_switch_)
                    this->su_kaynatma_switch_->publish_state(true);
                break;

            case MODE_MAMA_SUYU:
                this->touch_states_[0] = true;
                this->control_led(0);
                this->mama_suyu_durumu_ = MAMA_SUYU_HAZIRLIK;
                if (this->mama_suyu_switch_)
                    this->mama_suyu_switch_->publish_state(true);
                break;

            case MODE_CAY_DEMLEME:
                this->touch_states_[3] = true;
                this->visual_feedback_demleme_level(press_count);
                this->control_led(3);
                ESP_LOGI("CayseverRobotea", "Çay demleme işlemi başlıyor. Süre: %d saniye.", this->demleme_suresi_);
                this->cay_demleme_durumu_ = DEMLEME_HAZIRLIK;

                if (this->cay_demleme_select_ != nullptr &&
                    this->cay_demleme_select_->state != this->cay_demleme_state_)
                {
                    this->cay_demleme_state_ = this->cay_demleme_select_->state;
                    this->cay_demleme_select_->publish_state(this->cay_demleme_select_->state);
                }
                break;
            }

            // 4) Mod sensoru yayınla
            this->publish_mode_();
        }
        void CayseverRobotea::set_cay_demleme_select(select::Select *cay_demleme_select)
        {
            this->cay_demleme_select_ = cay_demleme_select;
            this->cay_demleme_select_->publish_state("OFF");
            this->cay_demleme_select_->add_on_state_callback([this](const std::string &value, size_t index)
                                                             {
            ESP_LOGI("CayseverRobotea", "on_cay_demleme_change %s", value.c_str());
            this->on_cay_demleme_change(value); });
        }

        void CayseverRobotea::update_cay_demleme(const std::string &level)
        {
            this->cay_demleme_state_ = level;

            int numeric_level = 0;
            bool level_state = false;
            if (level == "1/4")
            {
                level_state = true;
                numeric_level = 1;
            }
            else if (level == "2/4")
            {
                level_state = true;
                numeric_level = 2;
            }
            else if (level == "3/4")
            {
                level_state = true;
                numeric_level = 3;
            }
            else if (level == "MAX")
            {
                level_state = true;
                numeric_level = 4;
            }
            else if (level == "OFF")
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
                this->set_mode(MODE_NONE, 0);
            }
        }

    } // namespace caysever_robotea
} // namespace esphome
