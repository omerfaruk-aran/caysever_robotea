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

            this->kettle_durumu_ = NORMAL;
            this->update_all_sensors();
        }

        // loop metodu tanımı
        void CayseverRobotea::loop()
        {
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

            if (this->kettle_durumu_ == NORMAL || this->kettle_durumu_ == KRITIK)
            {
                this->check_water_level();
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

            if (this->kettle_durumu_ == KRITIK || this->previous_mode_ == KRITIK)
            {
                this->handle_critical_mode_leds(); // Kritik mod LED yanıp sönme
            }
            else if (this->kettle_durumu_ == KORUMA && this->current_mode_ != MODE_NONE && this->previous_mode_ == NORMAL)
            {
                this->handle_protection_mode_leds(); // Koruma mod LED yanıp sönme
            }

            this->handle_critical_sounds();
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
            if (this->current_time_ - last_blink_time >= 300) // 300ms yanıp sönme aralığı
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
            delay(3000);
            digitalWrite(this->dem_led_pin_, LOW);
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

                        case MODE_NONE:
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
                    return; // İki tuşa aynı anda basıldığında işlemi iptal et
                }
                // Çay Demleme'e basıldı (ON)
                touch_start_time = this->current_time_;
                ESP_LOGI("CayseverRobotea", "Çay Demleme basıldı (ON).");
            }
            else if (!touch_value && this->previous_touch_states_[3])
            {
                if (touch2)
                {
                    return; // İki tuşa aynı anda basıldığında işlemi iptal et
                }
                // Çay Demleme bırakıldı (KAPALI)
                unsigned long press_duration = this->current_time_ - touch_start_time;

                if (press_duration >= 1200)
                {
                    // 1,2 saniyeden fazla basılı tutuldu, işlem iptal ediliyor
                    ESP_LOGW("CayseverRobotea", "Çay Demleme: İşlem iptal edildi (1,2 saniye basılı tutuldu).");
                    this->set_mode(MODE_NONE, 0);
                    press_count = 0;
                }
                else
                {
                    this->play_button_sound();
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

            // Demleme süresini belirle kolay kullanım için ters çevrildi.(1=MAX, 2=3/4, 3=2/4, 4=1/4)
            switch (level)
            {
            case 1:
                this->demleme_suresi_ = 420; // 7 dakika (Maximum çizgisinin üzerinde doldurulabilir.)
                break;
            case 2:
                this->demleme_suresi_ = 300; // 5 dakika
                break;
            case 3:
                this->demleme_suresi_ = 210; // 3 dakika 30 saniye
                break;
            case 4:
                this->demleme_suresi_ = 120; // 2 dakika
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

            if (this->buton_sesi_switch_->state)
            {
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[2], true}, // GPIO32: HIGH
                    {this->sound_pins_[1], false} // GPIO19: LOW
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_mama_suyu_hazir_sound()
        {
            if (this->konusma_sesi_switch_->state)
            {
                // Mama suyu sesi: GPIO4, GPIO19 ve GPIO32 HIGH
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[1], true}, // GPIO19: LOW
                    {this->sound_pins_[2], true}  // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_cay_demleme_start_sound()
        {
            if (this->konusma_sesi_switch_->state)
            { // Çay demleme sesi: GPIO4 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true},  // GPIO4: HIGH
                    {this->sound_pins_[1], false}, // GPIO19: LOW
                    {this->sound_pins_[2], false}  // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_cay_demleme_done_sound()
        {
            if (this->konusma_sesi_switch_->state)
            { // Çay demleme tamam sesi: GPIO4 ve GPIO19 HIGH, GPIO32 LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], true}, // GPIO4: HIGH
                    {this->sound_pins_[1], true}, // GPIO19: LOW
                    {this->sound_pins_[2], false} // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_filtre_kahve_hazirlaniyor_sound()
        {
            if (this->konusma_sesi_switch_->state)
            { // Filtre kahve hazırlanıyor sesi: GPIO19 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], true},  // GPIO19: LOW
                    {this->sound_pins_[2], false}  // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_filtre_kahve_done_sound()
        {
            if (this->konusma_sesi_switch_->state)
            { // Filtre kahve tamam sesi: GPIO19 ve GPIO32 HIGH, GPIO4 LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], true},  // GPIO19: LOW
                    {this->sound_pins_[2], true}   // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
        }

        void CayseverRobotea::play_su_kaynadi_sound()
        {
            if (this->konusma_sesi_switch_->state)
            { // Su kaynadı sesi: GPIO32 HIGH, diğerleri LOW
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: HIGH
                    {this->sound_pins_[1], false}, // GPIO19: LOW
                    {this->sound_pins_[2], true}   // GPIO32: HIGH
                });
            }
            else
            {
                ESP_LOGI(TAG, "Buton sesi devre dışı, çalınmadı.");
            }
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
                if (temperature >= 100.0f)
                {
                    if (digitalRead(this->relay_pin_) != LOW)
                    {
                        digitalWrite(this->relay_pin_, LOW);
                    }
                    this->relay_active_ = false;
                    this->su_kaynatma_durumu_ = SU_KAYNATMA_SICAKLIK_KORUMA;
                    this->update_all_sensors();

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
                // Koruma Modu (90°C - 95°C)
                this->maintain_temperature(90.0f, 95.0f);

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
                // Koruma Modu (90°C - 95°C)
                this->maintain_temperature(90.0f, 95.0f);

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
                    if (digitalRead(this->relay_pin_) != LOW)
                    {
                        digitalWrite(this->relay_pin_, LOW);
                    }
                    this->relay_active_ = false;
                    this->last_relay_toggle_time_ = this->current_time_;
                    ESP_LOGI("CayseverRobotea", "Sıcaklık koruma: %.2f°C, Röle kapatıldı.", temperature);
                }
            }
        }

        void CayseverRobotea::check_water_level()
        {
            if (this->su_kontrol_switch_->state)
            {
                if (kettle_durumu_ == KORUMA)
                {
                    return; // Koruma modunda su seviyesi kontrol edilmez
                }

                static unsigned long relay_off_time = 0;
                static bool water_low = false;
                static float last_temperature = -1.0; // Önceki sıcaklık

                // Eğer manuel çıkış yapılmışsa gerekli sıfırlamaları yap
                if (this->manual_exit)
                {
                    ESP_LOGI("CayseverRobotea", "Manuel kritik moddan çıkış algılandı. Relay zamanlayıcı ve su seviyesi sıfırlanıyor.");
                    relay_off_time = 0;
                    water_low = false;
                    this->manual_exit = false; // Bayrağı sıfırla
                }

                float temperature = this->ntc_sensor_->state;

                if (!this->relay_active_)
                {
                    if (relay_off_time == 0)
                    {
                        relay_off_time = this->current_time_;
                    }

                    unsigned long elapsed_time = (this->current_time_ - relay_off_time) / 1000;
                    // ESP_LOGI("CayseverRobotea", "Relay kapalı geçen süre: %lu saniye", elapsed_time);

                    if (elapsed_time >= 20 && temperature >= 103.5f)
                    {
                        ESP_LOGE("CayseverRobotea", "Su seviyesi azalmış olabilir! Sıcaklık: %.2f°C", temperature);
                        water_low = true;
                    }
                }
                else
                {
                    if (relay_off_time != 0) // relay_off_time sadece relay tekrar aktif olduğunda sıfırlanır.
                    {
                        relay_off_time = 0;
                    }
                    water_low = false;
                }

                // Kritik duruma geçiş
                if (water_low && this->kettle_durumu_ == NORMAL)
                {
                    ESP_LOGE("CayseverRobotea", "Kritik: Su seviyesi çok azaldı! Koruma moduna geçiliyor.");
                    this->kettle_durumu_ = KRITIK;
                    this->update_all_sensors();

                    // Kritik mod seslerini başlat
                    this->kritik_sound_start_time_ = this->current_time_;
                    this->kritik_sound_active_ = true;

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

                // Kritik moddan çıkış koşulları
                if (this->kettle_durumu_ == KRITIK)
                {
                    if (last_temperature > 0 && (last_temperature - temperature) >= 5.0f)
                    {
                        ESP_LOGI("CayseverRobotea", "Kritik moddan çıkılıyor. Sıcaklık düşüşü algılandı (%.2f°C).", last_temperature - temperature);
                        this->kettle_durumu_ = NORMAL;
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

                        case MODE_NONE:
                            this->control_led(-1);
                            break;

                        default:
                            break;
                        }
                    }
                }

                last_temperature = temperature; // Son sıcaklık değerini güncelle
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
                case MODE_NONE:
                {
                    state_str = "NONE";
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
                    state_str = "NONE";
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
            static ActiveMode last_mode = MODE_NONE;
            static KettleDurumu last_kettle_state = NORMAL;

            if (this->current_mode_ != last_mode)
            {
                this->publish_mode_();
                last_mode = this->current_mode_;
            }
            if (this->kettle_durumu_ != last_kettle_state)
            {
                this->publish_kettle_state_();
                last_kettle_state = this->kettle_durumu_;
            }
            this->publish_mode_state_(); // Alt durum değişiklikleri için her zaman güncelle.
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
                    if (this->cay_demleme_select_->state != "KAPALI")
                    {
                        this->cay_demleme_select_->publish_state("KAPALI");
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

                // Select nesnesini güncelle
                if (this->cay_demleme_select_ != nullptr)
                {
                    std::string new_state = "1/4";
                    if (press_count == 1)
                    {
                        new_state = "MAX";
                    }
                    else if (press_count == 2)
                    {
                        new_state = "3/4";
                    }
                    else if (press_count == 3)
                    {
                        new_state = "2/4";
                    }
                    this->cay_demleme_state_ = new_state;
                    this->cay_demleme_select_->publish_state(new_state);
                }

                break;
            }

            // 4) Mod sensoru yayınla
            this->update_all_sensors();
        }
        void CayseverRobotea::set_cay_demleme_select(select::Select *cay_demleme_select)
        {
            this->cay_demleme_select_ = cay_demleme_select;
            this->cay_demleme_select_->publish_state("KAPALI");
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
                this->set_mode(MODE_NONE, 0);
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
