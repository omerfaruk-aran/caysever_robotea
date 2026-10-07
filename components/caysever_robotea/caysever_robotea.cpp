#include "caysever_robotea.h"
#include "esphome/core/log.h"
#include <WiFi.h>
#include <algorithm>
#include <cmath>

namespace esphome
{
    namespace caysever_robotea
    {
        static const char *const TAG = "caysever_robotea";

        volatile uint32_t CayseverRobotea::brew_sense_edges_ = 0;

        // Demleme hattı girişindeki her kenarda çalışır; yalnız sayar.
        void IRAM_ATTR CayseverRobotea::brew_sense_isr_()
        {
            brew_sense_edges_ = brew_sense_edges_ + 1;
        }

        void CayseverRobotea::setup()
        {
            // Röle pinleri her şeyden önce: aşağıdaki LED yanıp sönmesi 3 sn sürüyor, o sırada da kapalı sürülmüş olsunlar
            pinMode(this->relay_pin_, OUTPUT);
            digitalWrite(this->relay_pin_, LOW);
            pinMode(this->demleme_relay_pin_, OUTPUT);
            digitalWrite(this->demleme_relay_pin_, LOW);

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
            // Su seviye kontrolü ardışık okumalara baktığı için her NTC okumasını zamanıyla sakla
            if (this->ntc_sensor_ != nullptr)
            {
                this->ntc_sensor_->add_on_state_callback([this](float value)
                                                         { this->record_ntc_sample_(value); });
            }
            // Süzgeçsiz okuma verilmişse kettle'ın kaldırıldığı ondan, süzgecin gecikmesi beklenmeden anlaşılır
            if (this->ham_ntc_sensor_ != nullptr)
            {
                this->ham_ntc_sensor_->add_on_state_callback([this](float value)
                                                             { this->ham_ornek_(value); });
            }

            // Ses pinlerini çıkış olarak ayarla ve başlangıç durumunu LOW yap
            for (int i = 0; i < 3; i++) // Burada `sound_pins_` 3 elemanlı bir dizi
            {
                pinMode(this->sound_pins_[i], OUTPUT);
                digitalWrite(this->sound_pins_[i], LOW);
            }
            // "Su bitti" algısı istenmişse demleme hattı girişini dinlemeye başla (yalnız giriş, hiçbir şeyi sürmez)
            if (this->su_bitti_algisi_switch_ != nullptr)
            {
                pinMode(this->brew_sense_pin_, INPUT);
                attachInterrupt(digitalPinToInterrupt(this->brew_sense_pin_), CayseverRobotea::brew_sense_isr_, CHANGE);
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

                // NaN: ölçüm yok (açılışta ilk okuma öncesi / sensör hatası). Tüm karşılaştırmalar false
                // döndüğü için aşağıdaki mod işleyicileri röleyi açabilir; koruma moduna al.
                // ham_kettle_yok_: süzgeçsiz okuma kettle'ın kalktığını gösteriyor (süzgeçli değer henüz bozulmamış olabilir)
                if (std::isnan(temperature) || temperature < 0.0f || this->ham_kettle_yok_) // Anormal sıcaklık değeri
                {
                    if (this->kettle_durumu_ != KORUMA)
                    {
                        ESP_LOGW("CayseverRobotea", "Kettle kaldırıldı veya NTC sensör hatası. Koruma moduna geçiliyor.");
                        // Önceki durumu kaydet
                        this->previous_mode_ = kettle_durumu_;

                        this->kettle_durumu_ = KORUMA;
                        this->koruma_start_ms_ = this->current_time_;
                        this->ham_geri_ms_ = 0;
                        this->koruma_lamba_geri_ = false;
                        this->update_all_sensors();

                        // LED durumlarını kaydet
                        bayled_previous_state = digitalRead(this->bay_led_pin_);
                        demled_previous_state = digitalRead(this->dem_led_pin_);

                        // Kettle tabanda değilken bütün lambalar söner (fabrika yazılımındaki davranış): aktif modun
                        // tuş lambası ve tazelik (Dem/Bay) lambaları. Geri konunca aşağıda eski hâllerine dönerler.
                        this->control_led(-1);
                        digitalWrite(this->dem_led_pin_, LOW);
                        digitalWrite(this->bay_led_pin_, LOW);

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
                    else if (this->previous_mode_ != KRITIK)
                    {
                        // Kettle geri kondu (ham okuma görüyor) ama süzgeçli sıcaklık henüz toparlanmadı (1-3 sn sürer).
                        // Lambalar hemen eski hâllerine döner; ısıtma ve demleme, güvenilir sıcaklık gelene kadar
                        // (aşağıdaki NORMAL'e dönüş) başlamaz. Bu arada yeniden kaldırılırsa lambalar yine söner.
                        const bool ham_geri = this->ham_ntc_sensor_ != nullptr && !this->ham_kettle_yok_ && this->ham_geri_ms_ != 0;
                        if (ham_geri && !this->koruma_lamba_geri_)
                        {
                            digitalWrite(this->bay_led_pin_, bayled_previous_state);
                            digitalWrite(this->dem_led_pin_, demled_previous_state);
                            this->restore_mode_leds_();
                            this->koruma_lamba_geri_ = true;
                        }
                        else if (!ham_geri && this->koruma_lamba_geri_)
                        {
                            this->control_led(-1);
                            digitalWrite(this->dem_led_pin_, LOW);
                            digitalWrite(this->bay_led_pin_, LOW);
                            this->koruma_lamba_geri_ = false;
                        }
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
                            // Kettle'ın tabandan ayrı kaldığı süre. Ham algı devredeyse geri konduğu an ondan alınır:
                            // süzgeçli değerin toparlanması 1-3 sn daha sürer ve kısa bir kaldırışı uzun gösterirdi.
                            const uint32_t geri_ms = this->ham_geri_ms_ != 0 ? this->ham_geri_ms_ : this->current_time_;
                            if (geri_ms - this->koruma_start_ms_ >= KRITIK_ONAY_MS)
                            {
                                // Kettle bilerek kaldırılıp geri kondu: alarm onaylandı. KRITIK'e girerken bütün
                                // işlemler ve mod kapatılmıştı; cihaz boşta kalır, hiçbir şey kendiliğinden sürmez.
                                ESP_LOGI("CayseverRobotea", "Kettle kaldırılıp geri kondu: kritik durum onaylandı, cihaz boşta.");
                                this->reset_all_operations(true);
                                if (this->current_mode_ != MODE_KAPALI)
                                    this->set_mode(MODE_KAPALI, 0);
                                this->manual_exit = true;
                                this->kettle_durumu_ = NORMAL;
                                this->previous_mode_ = NORMAL;
                            }
                            else
                            {
                                // Kısa süreli ölçüm kaybı (ör. tek okumalık NaN): kritik koruma atlanmaz
                                ESP_LOGI("CayseverRobotea", "Kritik moda geri dönülüyor.");
                                this->kettle_durumu_ = KRITIK;
                                this->previous_mode_ = NORMAL;
                            }
                        }
                        else
                        {
                            ESP_LOGI("CayseverRobotea", "Normal moda geri dönülüyor.");
                            // BayLED ve DemLED'i eski durumlarına döndür, aktif modun tuş lambasını yeniden yak
                            digitalWrite(this->bay_led_pin_, bayled_previous_state);
                            digitalWrite(this->dem_led_pin_, demled_previous_state);
                            this->restore_mode_leds_();
                            this->kettle_durumu_ = NORMAL;
                            this->previous_mode_ = NORMAL;
                            this->brew_resume_after_koruma_();
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

            this->update_brew_sense_();

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

            // Kritik durumda (kettle tabandayken) LED'ler yanıp söner. Kettle kaldırılmışken (KORUMA) bütün lambalar sönüktür.
            if (this->kettle_durumu_ == KRITIK)
            {
                this->handle_critical_mode_leds(); // Kritik mod LED yanıp sönme
            }

            this->handle_critical_sounds();

            this->process_demleme_feedback_();

            this->check_auto_off_();

            this->publish_tazelik_();
        }
        void CayseverRobotea::handle_critical_sounds()
        {
            if (this->kritik_sound_active_ && this->kettle_durumu_ == KRITIK)
            {
                if (this->current_time_ - this->kritik_sound_start_time_ >= this->kritik_alarm_bekleme_ms_)
                {
                    this->activate_sound(std::map<int, bool>{
                        {this->sound_pins_[0], true},
                        {this->sound_pins_[2], true},
                        {this->sound_pins_[1], false}});
                    this->kritik_sound_start_time_ = this->current_time_;
                    this->kritik_alarm_bekleme_ms_ = KRITIK_ALARM_ARALIK_MS;
                }
            }
            else if (this->kettle_durumu_ == NORMAL && this->kritik_sound_active_)
            {
                // Yalnız kritik durumdan çıkılınca kapanır. Kettle kaldırılmışken (KORUMA) ses zaten çalmaz; kısa bir
                // ölçüm kaybından sonra KRITIK'e geri dönülürse alarm kaldığı yerden sürer.
                this->kritik_sound_active_ = false;
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
            this->handle_touch_input_filter_coffee();
        }

        // Tuş 2: filtre kahve aç/kapat (seviye yok). Tuş 1 ile birlikte basılı tutmak buton sesini açıp kapatan
        // kısayoldur; o durumda ve uzun basışta mod değişmez. Karar tuş bırakılınca verilir.
        void CayseverRobotea::handle_touch_input_filter_coffee()
        {
            static unsigned long touch_start_time = 0;
            static bool birlikte = false; // basılıyken tuş 1 de basıldı

            bool touch_value = digitalRead(this->touch_pins_[1]) == LOW;
            bool touch1 = digitalRead(this->touch_pins_[0]) == LOW;

            if (kettle_durumu_ == KRITIK)
            {
                this->previous_touch_states_[1] = touch_value;
                birlikte = false;
                return;
            }

            if (touch_value && !this->previous_touch_states_[1])
            {
                touch_start_time = this->current_time_;
                birlikte = touch1;
            }
            else if (touch_value && touch1)
            {
                birlikte = true;
            }
            else if (!touch_value && this->previous_touch_states_[1])
            {
                if (!birlikte && !touch1 && this->current_time_ - touch_start_time < 1200)
                {
                    this->play_button_sound();
                    this->update_filtre_kahve(!this->touch_states_[1]);
                }
                birlikte = false;
            }

            this->previous_touch_states_[1] = touch_value;
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

                        ESP_LOGI("CayseverRobotea", "Kritik moddan çıkılıyor. Cihaz boşta.");
                        this->manual_exit = true;
                        this->kettle_durumu_ = NORMAL; // Durumu NORMAL'e döndür
                        this->update_all_sensors();

                        // KRITIK'e girerken bütün işlemler ve mod kapatıldı; çıkışta lambalar da o duruma göre ayarlanır
                        digitalWrite(this->bay_led_pin_, LOW);
                        digitalWrite(this->dem_led_pin_, LOW);
                        this->restore_mode_leds_();
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

        // Aktif modun tuş lambasını aşamasına göre yakar: hazırlıkta kırmızı, hazır/sıcak tutmada beyaz; mod yoksa hepsi sönük.
        // Kettle geri konduğunda ve kritik durumdan çıkışta kullanılır.
        void CayseverRobotea::restore_mode_leds_()
        {
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
            case MODE_FILTRE_KAHVE:
                // Kaynatırken ve demlerken kırmızı, içecek hazır olunca (sıcak tutma) beyaz
                if (this->cay_demleme_durumu_ == DEMLEME_HAZIRLIK || this->cay_demleme_durumu_ == DEMLEME_BASLADI)
                {
                    this->control_led(this->demleme_led_(), false);
                }
                else if (this->cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA)
                {
                    this->control_led(this->demleme_led_(), true);
                }
                break;

            case MODE_KAPALI:
                this->control_led(-1);
                break;
            default:
                break;
            }
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
            static unsigned long touch_start_time = 0;  // Tuş basılma başlangıç zamanı
            static unsigned long last_release_time = 0; // Son bırakma zamanı
            static int press_count = 0;                 // Bas çek sayacı

            if (kettle_durumu_ == KRITIK)
            {
                // Alarmdan hemen önce yapılmış, henüz işlenmemiş basış unutulur; yoksa alarm onaylanınca
                // çay modu kendiliğinden başlardı.
                press_count = 0;
                return;
            }

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
                    // Fabrikadaki gibi: tuşa basılır basılmaz lamba kırmızı (mod 1 sn'lik basış sayma süresi dolunca başlar)
                    if (press_count == 1 && this->kettle_durumu_ == NORMAL)
                    {
                        this->control_led(3);
                    }
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
                    if (this->kettle_durumu_ == NORMAL)
                    {
                        this->restore_mode_leds_(); // ilk basışta yakılan kırmızı geri alınır
                    }
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

            if (level == 1)
            {
                // Tek basış (MAX) fabrikadaki gibi: seviye bildirimi yok. Lamba kırmızı kalır, ikinci bir bip çalmaz
                // (tuşun ya da Home Assistant komutunun bip'i zaten çaldı). Su zaten kaynamışsa demleme konuşması
                // o bip'in üstüne binmesin diye aynı nefes payı bırakılır.
                this->demleme_fb_.active = false;
                this->demleme_fb_end_ms_ = this->current_time_;
                return;
            }

            // 3/4, 2/4, 1/4 seçimlerinde seviye beyaz yanıp sönmeyle gösterilir, ardından onay bip'i çalar.
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
                this->demleme_fb_end_ms_ = this->current_time_;
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
            this->brew_phase_ = BREW_TIMED;
            this->brew_cycle_started_ = false;
            this->brew_pump_ms_ = 0;

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

        void CayseverRobotea::play_su_ekle_sound()
        {
            if (!this->konusma_sesi_switch_)
                return;

            if (this->konusma_sesi_switch_->state)
            { // "Hazneye su ekle..." uyarısı: GPIO19 ve GPIO32 HIGH, GPIO4 LOW. Fabrika yazılımı bu klibi hata
              // durumlarında çalar; filtre kahve bitince çalan klip çaydakiyle aynıdır ("içeceğiniz hazır").
                this->activate_sound(std::map<int, bool>{
                    {this->sound_pins_[0], false}, // GPIO4: LOW
                    {this->sound_pins_[1], true},  // GPIO19: HIGH
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

        void CayseverRobotea::ses_dene(uint8_t mask)
        {
            ESP_LOGI("CayseverRobotea", "Ses denemesi: maske %u", mask);
            this->activate_sound(std::map<int, bool>{
                {this->sound_pins_[0], (mask & 0x01) != 0},
                {this->sound_pins_[1], (mask & 0x02) != 0},
                {this->sound_pins_[2], (mask & 0x04) != 0}});
        }

        void CayseverRobotea::activate_sound(const std::map<int, bool> &pin_states)
        {
            // Pinleri set et
            for (const auto &entry : pin_states)
            {
                digitalWrite(entry.first, entry.second ? HIGH : LOW);
            }

            uint32_t token = ++this->sound_pulse_token_;
            this->set_timeout("sound_off", SOUND_PULSE_MS, [this, token]()
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
            const uint32_t now = this->current_time_;

            if (temperature >= OVERHEAT_CUTOFF_T)
            {
                ESP_LOGE("CayseverRobotea", "OVERHEAT! T=%.2fC. Röle kapatiliyor, KRITIK.", temperature);
                this->enter_critical_();
                return;
            }

            // Mod kettle kaldırılmışken başlatıldıysa sıcaklık ancak şimdi görülüyor: su zaten sıcaksa mod başlamaz.
            if (this->mama_ilk_okuma_)
            {
                this->mama_ilk_okuma_ = false;
                if (this->mama_suyu_durumu_ == MAMA_SUYU_HAZIRLIK && temperature > MAMA_BASLAMAZ_T)
                {
                    this->mama_reddet_(temperature);
                    this->set_mode(MODE_KAPALI, 0);
                    this->uyari_baslat_(0);
                    return;
                }
            }

            const bool hazir = this->mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA;
            const uint32_t el = now - this->mama_faz_ms_;

            switch (this->mama_faz_)
            {
            case MAMA_FAZ_VURUS:
                // Vuruş süresi dolunca (ya da okuma beklenmedik biçimde hedefi epey geçtiyse) ısıtıcı kapanır
                if (el >= this->mama_vurus_ms_ || temperature >= MAMA_HEDEF_T + 3.0f)
                {
                    this->mama_isitici_(false);
                    this->mama_vurus_ms_ = el; // gerçekleşen süre
                    this->mama_otur_(MAMA_OTURMA_MS);
                }
                else
                {
                    this->mama_isitici_(true); // kettle kaldırılıp konduysa röle bırakılmıştır
                }
                break;

            case MAMA_FAZ_OTUR:
                this->mama_isitici_(false);
                if (el < this->mama_bekleme_ms_)
                    break;
                // Okuma oturdu. Son vuruşun suyu kaç derece ısıttığı ölçülür; sonraki vuruş buna göre boyutlanır.
                if (this->mama_vurus_ms_ >= 1000)
                {
                    // Ölçülebilir bir artış yoksa (çok su, ısı kaybı) kazanç en küçük değere iner: vuruşlar büyür,
                    // ama bir önceki vuruşun iki katından hızlı büyüyemez.
                    float artis = temperature - this->mama_vurus_oncesi_t_;
                    if (artis < 0.1f)
                        artis = 0.1f;
                    float k = artis / (this->mama_vurus_ms_ / 1000.0f);
                    if (k < 0.2f)
                        k = 0.2f;
                    if (k > 4.0f)
                        k = 4.0f;
                    this->mama_kazanc_ = k;
                    this->mama_onceki_vurus_ms_ = this->mama_vurus_ms_;
                    this->mama_vurus_ms_ = 0;
                }
                this->mama_faz_ = MAMA_FAZ_OLC;
                this->mama_faz_ms_ = now;
                [[fallthrough]];

            case MAMA_FAZ_OLC:
                this->mama_isitici_(false);
                if (!hazir)
                {
                    if (temperature >= MAMA_HAZIR_ALT_T)
                    {
                        if (temperature <= MAMA_HAZIR_UST_T)
                        {
                            this->mama_suyu_durumu_ = MAMA_SUYU_SICAKLIK_KORUMA;
                            this->update_all_sensors();
                            this->control_led(0, true);         // Tuş 1’in beyaz LED’ini yak
                            this->led_white_active_ = true;
                            this->play_mama_suyu_hazir_sound(); // Mama suyu hazırlandı sesi
                            if (!this->mama_hazir_oldu_)
                            {
                                this->mama_hazir_oldu_ = true;
                                this->mama_hazir_ms_ = now;
                            }
                            ESP_LOGI("CayseverRobotea", "Mama suyu hazır (oturmuş okuma %.2f°C), sıcak tutmaya geçildi.", temperature);
                        }
                        // Bandın üstündeyse su soğuyana kadar beklenir; "hazır" denmez
                    }
                    else
                    {
                        this->mama_vurus_basla_(temperature, MAMA_HEDEF_T);
                    }
                }
                else
                {
                    if (temperature <= MAMA_YENIDEN_T)
                    {
                        // Su belirgin soğudu (ör. üstüne su eklendi): baştan ısıtılır, hazır olunca yeniden haber verilir
                        this->mama_suyu_durumu_ = MAMA_SUYU_HAZIRLIK;
                        this->update_all_sensors();
                        this->control_led(0);
                        this->led_white_active_ = false;
                        this->mama_kazanc_ = MAMA_KAZANC_ILK; // su miktarı değişmiş olabilir
                        this->mama_onceki_vurus_ms_ = 0;
                        this->mama_otur_(MAMA_ILK_BEKLEME_MS);
                        ESP_LOGI("CayseverRobotea", "Mama suyu soğudu (okuma %.2f°C), yeniden ısıtılıyor.", temperature);
                    }
                    else if (temperature <= MAMA_TUT_T)
                    {
                        this->mama_vurus_basla_(temperature, MAMA_HEDEF_T);
                    }
                }
                break;
            }

            // Hazır olduktan sonra sıcak tutma süresi dolduysa mod kapanır (yaml: mama_suyu_sicak_tutma)
            if (this->mama_hazir_oldu_ && this->mama_sicak_tutma_ms_ != 0 && !this->pending_mode_change_ &&
                now - this->mama_hazir_ms_ >= this->mama_sicak_tutma_ms_)
            {
                ESP_LOGW("CayseverRobotea", "Mama suyu %u dakikadır hazır; mod kapatılıyor.", (unsigned)(this->mama_sicak_tutma_ms_ / 60000));
                this->set_mode(MODE_KAPALI, 0);
            }
        }

        void CayseverRobotea::mama_isitici_(bool on)
        {
            if ((digitalRead(this->relay_pin_) == HIGH) == on)
            {
                this->relay_active_ = on;
                return;
            }
            digitalWrite(this->relay_pin_, on ? HIGH : LOW);
            this->relay_active_ = on;
            this->last_relay_toggle_time_ = this->current_time_;
        }

        // Vuruş: hedefe kalan farkın %80'i kadar ısıtacak süre. Kazanç (°C / vuruş saniyesi) önceki vuruştan ölçülür;
        // ilk vuruşta az su varmış gibi davranılır. Süre bir önceki vuruşun iki katından fazla büyüyemez.
        void CayseverRobotea::mama_vurus_basla_(float t, float hedef)
        {
            float sn = 0.8f * (hedef - t) / this->mama_kazanc_;
            if (this->mama_onceki_vurus_ms_ != 0)
            {
                const float sinir = 2.0f * (this->mama_onceki_vurus_ms_ / 1000.0f) + 2.0f;
                if (sn > sinir)
                    sn = sinir;
            }
            if (sn < MAMA_VURUS_MIN_SN)
                sn = MAMA_VURUS_MIN_SN;
            if (sn > MAMA_VURUS_MAX_SN)
                sn = MAMA_VURUS_MAX_SN;
            this->mama_vurus_oncesi_t_ = t;
            this->mama_vurus_ms_ = (uint32_t)(sn * 1000.0f);
            this->mama_faz_ = MAMA_FAZ_VURUS;
            this->mama_faz_ms_ = this->current_time_;
            this->mama_isitici_(true);
            ESP_LOGI("CayseverRobotea", "Mama suyu: okuma %.2f°C, %.1f sn vuruş (kazanç %.2f°C/sn).", t, sn, this->mama_kazanc_);
        }

        void CayseverRobotea::mama_otur_(uint32_t bekleme_ms)
        {
            this->mama_faz_ = MAMA_FAZ_OTUR;
            this->mama_faz_ms_ = this->current_time_;
            this->mama_bekleme_ms_ = bekleme_ms;
        }

        void CayseverRobotea::mama_reddet_(float t)
        {
            ESP_LOGW("CayseverRobotea", "Mama suyu başlatılmadı: su zaten sıcak (%.1f°C > %.1f°C).", t, MAMA_BASLAMAZ_T);
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
                    this->enter_critical_();
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

                    // Seviye geri bildirimi (beyaz yanıp sönme, ardından bip) bitmeden demlemeyi başlatma. Su zaten
                    // kaynamışsa demleme aynı anda başlıyor, 1-3 sn sonra gelen bip "çayı demlemeye başlıyorum"
                    // konuşmasını yarıda kesiyor ve lambayı yeniden kırmızıya çeviriyordu. Bip'in ardından ses çipine
                    // kısa bir nefes payı da bırakılır.
                    if (this->demleme_fb_.active || (this->current_time_ - this->demleme_fb_end_ms_) < DEMLEME_FB_GAP_MS)
                        break;

                    this->cay_demleme_durumu_ = DEMLEME_BASLADI;
                    this->update_all_sensors();

                    this->demleme_start_time_ = this->current_time_; // Başlangıç zamanını kaydet
                    ESP_LOGI("CayseverRobotea", "Sıcaklık: %.2f°C, Kaynama tamamlandı, çay demleme başladı.", temperature);

                    if (this->brew_sense_usable_())
                    {
                        // Fabrikadaki düzen: 16 sn kesintisiz itiş, sonra "10 sn açık, kısa ölçüm" döngüsü; su bitince durur
                        this->brew_phase_ = BREW_PUSH;
                        this->brew_phase_start_ms_ = this->current_time_;
                        this->brew_last_on_ms_ = this->current_time_;
                        this->brew_pump_ms_ = 0;
                        this->brew_cycle_started_ = false;
                        this->brew_set_relay_(true);
                        this->control_led(this->demleme_led_()); // demleme sürerken lamba kırmızı; hazır olunca beyaza döner
                        ESP_LOGI("CayseverRobotea", "Demleme: su bitti algısıyla yürütülüyor (üst sınır %u sn).", this->demleme_suresi_);
                    }
                    else
                    {
                        if (this->su_bitti_algisi_switch_ != nullptr && this->su_bitti_algisi_switch_->state)
                        {
                            ESP_LOGW("CayseverRobotea", "Demleme hattında işaret görülmedi; demleme süreyle yürütülecek (%u sn).", this->demleme_suresi_);
                        }
                        this->brew_phase_ = BREW_TIMED;
                        this->brew_set_relay_(true); // Demleme rölesini aç
                        this->control_led(this->demleme_led_()); // kırmızı
                    }

                    if (this->kahve_())
                        this->play_filtre_kahve_hazirlaniyor_sound();
                    else
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
                if (this->kettle_durumu_ != NORMAL)
                    return; // aşırı ısınma kesmesi her şeyi kapattı; aşağıdaki süre mantığı durumu yeniden kurmasın

                if (this->brew_phase_ != BREW_TIMED)
                {
                    this->handle_brew_cycle_();
                    break;
                }

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
                    else if (this->current_time_ - this->demleme_end_time_ >= (this->kahve_() ? KAHVE_DEMLENME_MS : 240000u))
                    {
                        this->finish_demleme_();
                    }
                }

                break;

            case DEMLEME_SICAKLIK_KORUMA:
                this->maintain_temperature(95.0f, 99.0f);
                if (this->kettle_durumu_ != NORMAL)
                    return; // aşırı ısınma kesmesi her şeyi kapattı

                // DemLED süresini kontrol et
                if (this->demled_active_)
                {
                    unsigned long elapsed_time = this->current_time_ - this->demled_start_time_;
                    // 60 dakika = 3.600.000 ms
                    if (elapsed_time >= this->tazelik_ms_())
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

        // Demleme bitti: sıcak tutmaya geç, Dem lambasını yak, haber ver, tazelik süresini başlat.
        void CayseverRobotea::finish_demleme_()
        {
            this->cay_demleme_durumu_ = DEMLEME_SICAKLIK_KORUMA;
            this->update_all_sensors();

            ESP_LOGI("CayseverRobotea", "Çay demleme işlemi tamamlandı.");

            // LED güncellemesi ve sesli uyarı ("hazır" klibi çay ve filtre kahve için ortaktır)
            this->control_led(this->demleme_led_(), true);
            if (digitalRead(this->dem_led_pin_) != HIGH)
            {
                digitalWrite(this->dem_led_pin_, HIGH);
            }

            this->play_cay_demleme_done_sound();
            this->demled_start_time_ = this->current_time_;
            this->demled_active_ = true;
        }

        void CayseverRobotea::brew_set_relay_(bool on)
        {
            digitalWrite(this->demleme_relay_pin_, on ? HIGH : LOW);
            this->dem_relay_active_ = on;
        }

        // Algı kullanılabilir mi: yaml'da anahtar verilmiş ve açık, girişte bu açılışta kenar görülmüş, arıza yok.
        bool CayseverRobotea::brew_sense_usable_() const
        {
            return this->su_bitti_algisi_switch_ != nullptr && this->su_bitti_algisi_switch_->state &&
                   this->brew_sense_trusted_ && !this->brew_sense_fault_;
        }

        // Demleme hattı girişini 1 sn'lik pencerelerle izler. Demleme rölesi bırakılmış ve kettle yerindeyken kenar
        // görülmüşse algıya güvenilir (o âna kadar demleme eski, süreli düzenle yürür). Tanılama sensörünü besler.
        void CayseverRobotea::update_brew_sense_()
        {
            if (this->su_bitti_algisi_switch_ == nullptr || this->brew_sense_fault_)
                return;

            // Pencere "temiz" sayılır: boyunca demleme rölesi bırakılmış ve demleme döngüsü çalışmıyor (döngü kendi
            // ölçümünü ayrıca yayınlar). Güven için ayrıca kettle'ın yerinde olması aranır.
            const bool relay_off_now = (digitalRead(this->demleme_relay_pin_) == LOW) &&
                                       !(this->cay_demleme_durumu_ == DEMLEME_BASLADI && this->brew_phase_ != BREW_TIMED &&
                                         this->brew_phase_ != BREW_STEEP);
            const bool clean_now = relay_off_now && (this->kettle_durumu_ == NORMAL);

            if (!this->brew_win_open_)
            {
                this->brew_win_open_ = true;
                this->brew_win_start_ms_ = this->current_time_;
                this->brew_win_base_ = brew_sense_edges_;
                this->brew_win_clean_ = clean_now;
                this->brew_win_relay_off_ = relay_off_now;
                return;
            }
            if (!clean_now)
                this->brew_win_clean_ = false;
            if (!relay_off_now)
                this->brew_win_relay_off_ = false;

            const uint32_t el = this->current_time_ - this->brew_win_start_ms_;
            if (el < 1000)
                return;

            const uint32_t n = brew_sense_edges_ - this->brew_win_base_;
            const uint32_t rate = (uint32_t)(((uint64_t)n * 1000u) / el);
            this->brew_win_open_ = false;

            if (rate > BREW_STORM_RATE)
            {
                // Şebeke işareti saniyede ~100 kenar üretir. Bunun çok üstü gürültü ya da başka bir devredir: girişi
                // dinlemeyi bırak, demleme süreli düzenle yürüsün.
                detachInterrupt(digitalPinToInterrupt(this->brew_sense_pin_));
                this->brew_sense_fault_ = true;
                this->brew_sense_trusted_ = false;
                ESP_LOGE("CayseverRobotea", "Demleme hattı girişinde %u kenar/sn görüldü; su bitti algısı devre dışı bırakıldı.", (unsigned)rate);
                if (this->demleme_hatti_sensor_ != nullptr)
                    this->demleme_hatti_sensor_->publish_state(NAN);
                return;
            }

            if (this->brew_win_clean_ && rate >= BREW_TRUST_MIN_RATE && !this->brew_sense_trusted_)
            {
                this->brew_sense_trusted_ = true;
                ESP_LOGI("CayseverRobotea", "Demleme hattı işareti görüldü (%u kenar/sn); su bitti algısı kullanılabilir.", (unsigned)rate);
            }

            // Röle bırakılmışken ölçülen pencereler yayınlanır (röle açıkken hatta işaret olmaz, bilgi taşımaz)
            if (this->brew_win_relay_off_)
                this->publish_brew_rate_(rate);
        }

        // Tanılama sensörü: yalnız anlamlı değişimde yayınlanır (işaret var/yok değişti ya da hız 100'den çok oynadı).
        // Cihazda boştayken 120-150 arasında oynuyor; her oynamada yayınlamak Home Assistant geçmişini doldurur.
        void CayseverRobotea::publish_brew_rate_(uint32_t rate, bool force)
        {
            if (this->demleme_hatti_sensor_ == nullptr)
                return;
            const int bucket = (int)((rate + 5) / 10 * 10);
            const bool present = rate >= BREW_TRUST_MIN_RATE;
            const bool was_present = this->brew_rate_published_ >= (int)BREW_TRUST_MIN_RATE;
            const int diff = bucket > this->brew_rate_published_ ? bucket - this->brew_rate_published_ : this->brew_rate_published_ - bucket;
            if (force || this->brew_rate_published_ < 0 || present != was_present || diff >= 100)
            {
                this->demleme_hatti_sensor_->publish_state((float)bucket);
                this->brew_rate_published_ = bucket;
            }
        }

        // Fabrikadaki demleme döngüsü. DEMLEME_BASLADI içinde, kettle yerindeyken her turda çağrılır.
        void CayseverRobotea::handle_brew_cycle_()
        {
            const uint32_t now = this->current_time_;
            const uint32_t el = now - this->brew_phase_start_ms_;

            switch (this->brew_phase_)
            {
            case BREW_PUSH:
            case BREW_ON:
            {
                // Kettle kaldırılıp konduysa röle KORUMA'da bırakılmıştır; aşamanın kalanında yeniden çekilir
                if (digitalRead(this->demleme_relay_pin_) != HIGH)
                    this->brew_set_relay_(true);

                const uint32_t limit = (this->brew_phase_ == BREW_PUSH) ? BREW_PUSH_MS : BREW_ON_MS;
                if (el >= limit)
                {
                    this->brew_set_relay_(false);
                    this->brew_pump_ms_ += el;
                    if (!this->brew_cycle_started_)
                    {
                        this->brew_cycle_started_ = true;
                        this->brew_cycle_start_ms_ = now;
                    }
                    this->brew_phase_ = BREW_SENSE;
                    this->brew_phase_start_ms_ = now;
                    this->brew_sense_base_taken_ = false;
                }
                break;
            }

            case BREW_SENSE:
            {
                if (!this->brew_sense_base_taken_)
                {
                    if (el >= BREW_SENSE_SETTLE_MS)
                    {
                        this->brew_sense_base_ = brew_sense_edges_;
                        this->brew_sense_base_taken_ = true;
                    }
                    break;
                }
                if (el < BREW_SENSE_SETTLE_MS + BREW_SENSE_WINDOW_MS)
                    break;

                const uint32_t n = brew_sense_edges_ - this->brew_sense_base_;
                const bool water = n >= BREW_SENSE_MIN_EDGES;
                const bool cap = this->brew_pump_ms_ >= (uint32_t)this->demleme_suresi_ * 1000u;
                // Her ölçümün sonucu tanılama sensöründe görünsün (kenar/sn'ye çevrilmiş)
                this->publish_brew_rate_((uint32_t)(((uint64_t)n * 1000u) / BREW_SENSE_WINDOW_MS), true);

                if (water && !cap)
                {
                    this->brew_set_relay_(true);
                    this->brew_phase_ = BREW_ON;
                    this->brew_phase_start_ms_ = now;
                    this->brew_last_on_ms_ = now;
                    break;
                }

                if (water)
                {
                    ESP_LOGW("CayseverRobotea", "Demleme: hatta hâlâ işaret var ama üst sınıra (%u sn) varıldı; pompalama durduruldu.", this->demleme_suresi_);
                }
                else
                {
                    ESP_LOGI("CayseverRobotea", "Demleme: hatta işaret yok (%u kenar), su bitmiş görünüyor (pompalama %u sn).", (unsigned)n, (unsigned)(this->brew_pump_ms_ / 1000));
                    if (now - this->brew_cycle_start_ms_ <= BREW_EARLY_MS)
                    {
                        this->brew_fail_();
                        return;
                    }
                }
                ESP_LOGI("CayseverRobotea", "Demleme: su aktarımı bitti, demlenme bekleniyor.");
                this->brew_phase_ = BREW_STEEP;
                this->brew_phase_start_ms_ = now;
                break;
            }

            case BREW_STEEP:
                if (now - this->brew_last_on_ms_ >= this->demlenme_ms_())
                {
                    this->finish_demleme_();
                }
                break;

            default:
                break;
            }
        }

        // Kettle demleme sırasında kaldırılıp geri kondu. KORUMA'ya girerken demleme rölesi bırakılmıştı (demlik kettle'ın
        // üstünde durduğu için kettle yokken su aktarılmaz); geri konunca aktarıma yeni bir açık aşamayla devam edilir.
        // (Demleme hattındaki işaret kettle'dan bağımsızdır: cihazda kettle kaldırılmışken de sürdüğü ölçüldü.)
        void CayseverRobotea::brew_resume_after_koruma_()
        {
            if (this->cay_demleme_durumu_ != DEMLEME_BASLADI)
                return;

            if (this->brew_phase_ == BREW_TIMED)
            {
                // Süreli düzen: KORUMA'ya girerken demleme rölesi bırakıldı. Süre dolmadıysa yeniden çekilir; yoksa
                // kettle bir kez kaldırılınca su aktarımı o demlemede bir daha başlamıyordu.
                if (this->current_time_ - this->demleme_start_time_ < this->demleme_suresi_ * 1000)
                {
                    ESP_LOGI("CayseverRobotea", "Demleme: kettle geri kondu, su aktarımı sürüyor.");
                    this->brew_set_relay_(true);
                }
                return;
            }

            if (this->brew_phase_ != BREW_PUSH && this->brew_phase_ != BREW_ON && this->brew_phase_ != BREW_SENSE)
                return;

            if (this->brew_phase_ == BREW_PUSH || this->brew_phase_ == BREW_ON)
            {
                // Kaldırılana kadar röle açıktı; o süreyi üst sınır hesabına kat
                const uint32_t ran = this->koruma_start_ms_ - this->brew_phase_start_ms_;
                if (ran <= BREW_PUSH_MS)
                    this->brew_pump_ms_ += ran;
            }
            ESP_LOGI("CayseverRobotea", "Demleme: kettle geri kondu, su aktarımı sürüyor.");
            this->brew_phase_ = BREW_ON;
            this->brew_phase_start_ms_ = this->current_time_;
            this->brew_last_on_ms_ = this->current_time_;
            this->brew_set_relay_(true);
        }

        // Üst haznede su yok: demleme başlar başlamaz bitti (ilk ölçümlerde hatta işaret kalmadı). Aktarılacak su
        // olmadığına göre demlenme beklenmez: doğrudan "çay hazır" denir, sıcak tutma ve tazelik süresi başlar.
        // Demleme yapılamadı: su aktarımı daha ilk dakikada bitti. Üst hazne boştur ya da su demleme ısıtıcısına
        // ulaşmıyordur (ör. hazne ya da başlık yerine oturmamış); cihaz ikisini ayıramaz. Fabrika yazılımındaki gibi
        // hata sayılır: demleme ve kettle ısıtıcısı kapanır, mod kapanır, "çay demlendi" denmez, tazelik başlamaz.
        void CayseverRobotea::brew_fail_()
        {
            ESP_LOGW("CayseverRobotea", "Demleme yapılamadı: su aktarımı ilk %u sn içinde bitti (üst hazne boş ya da su ısıtıcıya ulaşmıyor). Çay demleme kapatılıyor.", (unsigned)(BREW_EARLY_MS / 1000));
            this->brew_set_relay_(false);
            digitalWrite(this->relay_pin_, LOW);
            this->relay_active_ = false;
            this->brew_failed_ = true;
            const int led = this->demleme_led_();

            // Mod bir sonraki turda kapanır; o âna kadar bu karar yinelenmesin
            this->brew_phase_ = BREW_STEEP;
            this->brew_last_on_ms_ = this->current_time_;
            this->set_mode(MODE_KAPALI, 0);

            this->uyari_baslat_(led, true);
        }

        // Uyarı: verilen tuşun lambası üç kez kırmızı yanıp söner. Ses: su_ekle istenmiş ve "Konuşma Sesi" açıksa fabrika
        // yazılımının bu durumda çaldığı "hazneye su ekle..." klibi (bir kez); değilse üç bip. İlk adım, modun kapanıp
        // lambaların söndüğü turdan ve tuşun/komutun kendi bip'inden sonraya bırakılır.
        void CayseverRobotea::uyari_baslat_(int led, bool su_ekle)
        {
            this->uyari_led_ = led;
            this->uyari_su_ekle_ = su_ekle && this->konusma_sesi_switch_ != nullptr && this->konusma_sesi_switch_->state;
            this->uyari_adim_no_ = 0;
            this->set_timeout("uyari_1a", 400, [this]()
                              { this->uyari_adimi_(true); });
            this->set_timeout("uyari_1k", 600, [this]()
                              { this->uyari_adimi_(false); });
            this->set_timeout("uyari_2a", 800, [this]()
                              { this->uyari_adimi_(true); });
            this->set_timeout("uyari_2k", 1000, [this]()
                              { this->uyari_adimi_(false); });
            this->set_timeout("uyari_3a", 1200, [this]()
                              { this->uyari_adimi_(true); });
            this->set_timeout("uyari_3k", 1400, [this]()
                              { this->uyari_adimi_(false); });
        }

        void CayseverRobotea::uyari_adimi_(bool on)
        {
            if (on)
            {
                this->uyari_adim_no_++;
                if (this->uyari_su_ekle_)
                {
                    if (this->uyari_adim_no_ == 1)
                        this->play_su_ekle_sound();
                }
                else
                {
                    // Uyarı olduğu için "Buton Sesi" anahtarına bakılmaz (KRITIK alarmı gibi)
                    this->activate_sound(std::map<int, bool>{
                        {this->sound_pins_[0], true},
                        {this->sound_pins_[2], true},
                        {this->sound_pins_[1], false}});
                }
            }
            // Lamba yalnız kettle yerindeyken ve araya yeni bir mod girmemişken oynatılır
            if (this->kettle_durumu_ != NORMAL || this->current_mode_ != MODE_KAPALI || this->uyari_led_ < 0)
                return;
            this->control_led(on ? this->uyari_led_ : -1);
        }

        void CayseverRobotea::check_auto_off_()
        {
            if (this->otomatik_kapanma_ms_ == 0 || this->current_mode_ == MODE_KAPALI || this->pending_mode_change_)
                return;
            if (this->current_time_ - this->mode_start_ms_ < this->otomatik_kapanma_ms_)
                return;

            ESP_LOGW("CayseverRobotea", "Mod %u dakikadır açık; cihaz kendiliğinden kapatılıyor.", (unsigned)(this->otomatik_kapanma_ms_ / 60000));
            this->set_mode(MODE_KAPALI, 0);
        }

        void CayseverRobotea::maintain_temperature(float min, float max)
        {
            float t = this->ntc_sensor_->state;

            // Global overheat guard (HER MODDA)
            if (t >= OVERHEAT_CUTOFF_T)
            {
                ESP_LOGE("CayseverRobotea", "OVERHEAT! T=%.2fC. Röle kapatiliyor, KRITIK.", t);
                this->enter_critical_();
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
            if (this->kettle_durumu_ == KORUMA)
                return;

            float temperature = this->ntc_sensor_->state;
            static float last_temperature_for_exit = -1.0;
            static uint32_t last_boil_finished_time = 0; // Kaynama bitiş zamanı

            // --- 1. KRİTİK MODDAN ÇIKIŞ VE MANUEL RESET ---
            if (this->manual_exit)
            {
                this->wl_win_start_ms_ = 0;
                this->manual_exit = false;
            }

            if (this->kettle_durumu_ == KRITIK)
            {
                if (last_temperature_for_exit > 0 && (last_temperature_for_exit - temperature) >= 5.0f)
                {
                    ESP_LOGI("CayseverRobotea", "Sıcaklık düşüşü algılandı, KRITIK moddan çıkılıyor.");
                    this->kettle_durumu_ = NORMAL;
                    this->update_all_sensors();
                }
                last_temperature_for_exit = temperature;
                return;
            }
            last_temperature_for_exit = temperature;

            // --- 2. KAYNAMA SONRASI SOĞUMA SÜRESİ ---
            // Eğer su yeni kaynadıysa, 30 saniye boyunca "susuz" kontrolü yapma.
            // Çünkü rezistansın ısısı sensörü 104-105 derecelere fırlatabiliyor (Overshoot).
            if (this->su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA ||
                this->mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA)
            {
                if (last_boil_finished_time == 0)
                    last_boil_finished_time = this->current_time_;

                // Kaynamadan sonraki ilk 30 saniye hassas ölçüm yapma
                if (this->current_time_ - last_boil_finished_time < 30000)
                    return;
            }
            else
            {
                last_boil_finished_time = 0;
            }

            // --- 3. SU YOK ANALİZİ ---
            bool water_low_detected = false;

            // A) Statik limit: taban bu sıcaklığı görüyorsa su yoktur.
            // Su varken de aşım görülebiliyor: kaynama sonrası 103.5 °C, soğuktan kaynatmadaki "steam boost" sırasında
            // 106.4 °C ölçüldü (gerçek cihaz, ~1 L su; eski 106 °C sınırı yanlış alarm verdi). Sınır fabrika yazılımının
            // kullandığı değere çekildi; kuru kettle saniyede birkaç derece ısındığı için kesme en çok bir okuma gecikir.
            if (temperature >= WL_STATIC_LIMIT_T)
            {
                ESP_LOGE("CayseverRobotea", "KRİTİK SICAKLIK: %.2f°C - Rezistans aşırı ısındı!", temperature);
                water_low_detected = true;
            }

            // B) Eğim (Slope) Kontrolü
            if (this->relay_active_ && !water_low_detected && this->mama_suyu_durumu_ == MAMA_SUYU_KAPALI)
            {
                if (this->wl_win_start_ms_ == 0)
                {
                    this->wl_win_start_ms_ = this->current_time_;
                    this->wl_win_start_t_ = temperature;
                }
                else
                {
                    uint32_t dt = this->current_time_ - this->wl_win_start_ms_;

                    if (dt >= 7000)
                    { // 7 saniyelik analiz
                        float diff = temperature - this->wl_win_start_t_;
                        float slope = diff / (dt / 1000.0f);

                        // Eşik Değeri: 1.65 C/sn
                        // Senin logunda 0.5L su 1.41 hızındaydı (kurtuldu).
                        // 0.1L su ise 2.26 hızındaydı (yakalanacak).
                        if (slope > 1.65f && temperature < 98.0f)
                        {
                            // Uç-nokta eğimi tek bir bozuk okumayla da büyür (ör. kettle–taban temasının anlık
                            // zayıflaması pencerenin ucuna denk gelirse). Gerçek hızlı ısınmada artış okumadan
                            // okumaya sürer; alarm vermeden önce penceredeki ardışık okumalara da bakılır.
                            if (this->slope_is_sustained_())
                            {
                                ESP_LOGE("CayseverRobotea", "HIZLI ISINMA: %.3f C/sn - Su yetersiz!", slope);
                                water_low_detected = true;
                            }
                            else
                            {
                                ESP_LOGW("CayseverRobotea", "Eğim %.3f C/sn ama artış sürekli değil (okuma sıçraması); alarm verilmedi.", slope);
                            }
                        }

                        this->wl_win_start_ms_ = this->current_time_;
                        this->wl_win_start_t_ = temperature;
                    }
                }
            }
            else
            {
                this->wl_win_start_ms_ = 0;
            }

            // --- 4. HATA TETİKLEME ---
            if (water_low_detected)
            {
                this->enter_critical_(KRITIK_SEBEP_SU_AZ);
            }
        }

        // Süzgeçsiz NTC okumasının her örneği. Ardışık HAM_KETTLE_ORNEK örnek aynı şeyi söyleyince karar değişir.
        void CayseverRobotea::ham_ornek_(float value)
        {
            const bool yok = std::isnan(value) || value < 0.0f;
            if (yok)
            {
                this->ham_var_sayac_ = 0;
                if (this->ham_yok_sayac_ < HAM_KETTLE_ORNEK)
                    this->ham_yok_sayac_++;
                if (this->ham_yok_sayac_ >= HAM_KETTLE_ORNEK)
                    this->ham_kettle_yok_ = true;
            }
            else
            {
                this->ham_yok_sayac_ = 0;
                if (this->ham_var_sayac_ < HAM_KETTLE_ORNEK)
                    this->ham_var_sayac_++;
                if (this->ham_var_sayac_ >= HAM_KETTLE_ORNEK && this->ham_kettle_yok_)
                {
                    this->ham_kettle_yok_ = false;
                    this->ham_geri_ms_ = millis();
                }
            }
        }

        // NTC'nin her yeni okumasını zamanıyla halka tampona yazar (ölçüm yoksa yazmaz).
        void CayseverRobotea::record_ntc_sample_(float value)
        {
            if (std::isnan(value))
                return;
            this->ntc_samples_[this->ntc_sample_head_] = NtcSample{millis(), value};
            this->ntc_sample_head_ = (this->ntc_sample_head_ + 1) % NTC_SAMPLE_COUNT;
            if (this->ntc_sample_len_ < NTC_SAMPLE_COUNT)
                this->ntc_sample_len_++;
        }

        // Su seviye penceresindeki (wl_win_start_ms_ .. şimdi) ardışık okumalara bakar. Pencere başında geçerli olan
        // okuma (başlangıçtan önceki son okuma) dâhil edilir. WL_MIN_INTERVALS'tan az aralık varsa karar verilemez ve
        // true döner (eski davranış). Aksi hâlde artış "sürekli" sayılmak için:
        //   - hiçbir aralıkta WL_GLITCH_DROP'tan hızlı düşüş olmamalı (ısıtıcı açıkken fiziksel değil, bozuk okumadır),
        //   - aralık eğimlerinin ortancası en az WL_SUSTAIN_MIN_MEDIAN olmalı (artış tek bir sıçramadan ibaret değil).
        bool CayseverRobotea::slope_is_sustained_()
        {
            float slopes[NTC_SAMPLE_COUNT];
            uint8_t n = 0;
            bool have_prev = false;
            NtcSample prev{};

            for (uint8_t i = 0; i < this->ntc_sample_len_; i++)
            {
                uint8_t idx = (this->ntc_sample_head_ + NTC_SAMPLE_COUNT - this->ntc_sample_len_ + i) % NTC_SAMPLE_COUNT;
                const NtcSample &s = this->ntc_samples_[idx];

                if ((int32_t)(s.ms - this->wl_win_start_ms_) <= 0)
                {
                    // Pencere başlamadan önceki okuma: yalnız sonuncusu başlangıç değeri olarak tutulur
                    prev = s;
                    have_prev = true;
                    continue;
                }
                if (have_prev && s.ms != prev.ms)
                {
                    slopes[n++] = (s.t - prev.t) / ((s.ms - prev.ms) / 1000.0f);
                }
                prev = s;
                have_prev = true;
            }

            if (n < WL_MIN_INTERVALS)
                return true;

            for (uint8_t i = 0; i < n; i++)
            {
                if (slopes[i] <= WL_GLITCH_DROP)
                    return false;
            }

            std::sort(slopes, slopes + n);
            float median = (n % 2) ? slopes[n / 2] : (slopes[n / 2 - 1] + slopes[n / 2]) / 2.0f;
            return median >= WL_SUSTAIN_MIN_MEDIAN;
        }

        // KRITIK'e geçiş tek yerden yapılır: ısıtma ve demleme röleleri kapanır, bütün işlemler ve mod sıfırlanır,
        // alarm başlar. Çıkışta (1. tuşa uzun basış ya da kettle'ı kaldırıp geri koyma) cihaz boşta kalır; hiçbir mod
        // kendiliğinden devam etmez.
        void CayseverRobotea::enter_critical_(KritikSebep sebep)
        {
            digitalWrite(this->relay_pin_, LOW);
            digitalWrite(this->demleme_relay_pin_, LOW);
            this->relay_active_ = false;
            this->dem_relay_active_ = false;

            this->kettle_durumu_ = KRITIK;
            this->kritik_sound_active_ = true;
            this->kritik_sound_start_time_ = this->current_time_;
            this->kritik_alarm_bekleme_ms_ = KRITIK_ALARM_ARALIK_MS;
            this->play_button_sound();

            // Su yetersizliği: "Konuşma Sesi" açıksa fabrika yazılımının bu durumda çaldığı "su ekleyin" klibi bir kez
            // çalınır ve alarm klip bitene kadar bekler. Konuşma kapalıysa ya da sebep başkaysa alarm eskisi gibidir.
            if (sebep == KRITIK_SEBEP_SU_AZ && this->konusma_sesi_switch_ != nullptr && this->konusma_sesi_switch_->state)
            {
                ESP_LOGW("CayseverRobotea", "KRITIK: su yetersiz; \"su ekleyin\" uyarısı çalınacak.");
                this->kritik_alarm_bekleme_ms_ = KRITIK_SU_EKLE_ALARM_MS;
                this->set_timeout("kritik_su_ekle", KRITIK_SU_EKLE_KLIP_MS, [this]()
                                  {
                    // Arada alarm onaylandıysa ya da kettle kaldırıldıysa konuşulmaz
                    if (this->kettle_durumu_ == KRITIK)
                        this->play_su_ekle_sound(); });
            }

            // Switchleri ve donanımı kapat
            if (this->su_kaynatma_switch_)
                this->su_kaynatma_switch_->publish_state(false);
            if (this->mama_suyu_switch_)
                this->mama_suyu_switch_->publish_state(false);

            this->reset_all_operations(true);
            // Mod da kapanır: mod sensörü, demleme seçicisi ve anahtarlar bir sonraki döngüde KAPALI yayınlanır
            this->set_mode(MODE_KAPALI, 0);
            this->update_all_sensors();
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
        void CayseverRobotea::set_filtre_kahve_switch(switch_::Switch *filtre_kahve_switch)
        {
            this->filtre_kahve_switch_ = filtre_kahve_switch;
            this->filtre_kahve_switch_->add_on_state_callback([this](bool state)
                                                              {
            ESP_LOGI("CayseverRobotea", "on_filtre_kahve_change %s", state ? "true" : "false");
            this->on_filtre_kahve_change(state); });
        }

        void CayseverRobotea::update_filtre_kahve(bool filtre_kahve)
        {
            if (filtre_kahve)
            {
                this->set_mode(MODE_FILTRE_KAHVE, 0);
            }
            else
            {
                this->set_mode(MODE_KAPALI, 0);
            }
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
            case MODE_FILTRE_KAHVE:
                return "FILTRE_KAHVE";
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
                const char *state_str = "KAPALI"; // iç switch'lerden hiçbiri eşleşmezse tanımsız kalmasın

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
                case MODE_FILTRE_KAHVE:
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

        void CayseverRobotea::publish_tazelik_()
        {
            if (this->tazelik_sensor_ == nullptr && this->tazelik_kalan_sensor_ == nullptr)
                return;

            // Tazelik, demleme bitince başlayan mevcut DemLED zamanlayıcısından türetiliyor:
            // DEMLEME_SICAKLIK_KORUMA + demled_active_ => Taze, süre dolunca (demled_active_ false) => Bayat
            int durum = 0; // Yok
            int kalan = -1; // NAN
            if (this->cay_demleme_durumu_ == DEMLEME_HAZIRLIK || this->cay_demleme_durumu_ == DEMLEME_BASLADI)
            {
                durum = 1; // Demleniyor
            }
            else if (this->cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA)
            {
                if (this->demled_active_)
                {
                    uint32_t gecen = this->current_time_ - this->demled_start_time_;
                    const uint32_t taze_ms = this->tazelik_ms_();
                    uint32_t kalan_ms = gecen < taze_ms ? taze_ms - gecen : 0;
                    durum = 2; // Taze
                    kalan = (kalan_ms + 59999) / 60000; // yukarı yuvarla: son dakikada 1 göster
                }
                else
                {
                    durum = 3; // Bayat
                    kalan = 0;
                }
            }

            // Son demleme yapılamadıysa yeni bir mod başlatılana kadar sebep Home Assistant'ta görünsün
            if (durum == 0 && this->brew_failed_)
            {
                durum = 4; // Demlenemedi
            }

            if (this->tazelik_sensor_ != nullptr && durum != this->tazelik_son_durum_)
            {
                static const char *const DURUMLAR[] = {"Yok", "Demleniyor", "Taze", "Bayat", "Demlenemedi"};
                this->tazelik_sensor_->publish_state(DURUMLAR[durum]);
                this->tazelik_son_durum_ = durum;
            }

            if (this->tazelik_kalan_sensor_ != nullptr && kalan != this->tazelik_son_kalan_)
            {
                this->tazelik_kalan_sensor_->publish_state(kalan < 0 ? NAN : static_cast<float>(kalan));
                this->tazelik_son_kalan_ = kalan;
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

            // Kritik durumda yeni mod başlatılmaz (alarm önce onaylanmalı). İstek reddedilir; Home Assistant'tan
            // açılan anahtar ya da seçici de kapalıya geri çekilir ki orada "çalışıyor" görünmesin.
            if (this->kettle_durumu_ == KRITIK && new_mode != MODE_KAPALI)
            {
                ESP_LOGW("CayseverRobotea", "Kritik durumda mod başlatılamaz (istenen: %d); istek reddedildi.", new_mode);
                new_mode = MODE_KAPALI;
                press_count = 0;

                if (this->su_kaynatma_switch_ && this->su_kaynatma_switch_->state)
                    this->su_kaynatma_switch_->publish_state(false);
                if (this->mama_suyu_switch_ && this->mama_suyu_switch_->state)
                    this->mama_suyu_switch_->publish_state(false);
                if (this->filtre_kahve_switch_ && this->filtre_kahve_switch_->state)
                    this->filtre_kahve_switch_->publish_state(false);
                if (this->cay_demleme_select_ != nullptr && this->cay_demleme_select_->current_option() != "KAPALI")
                    this->cay_demleme_select_->publish_state("KAPALI");
            }

            // Mama suyu sıcak suyla başlatılmaz (fabrika yazılımındaki gibi): ısıtılacak bir şey yoktur ve "mama suyu
            // hazır" demek yanlış olur. İstek reddedilir, her şey kapanır, uyarı verilir.
            bool mama_reddedildi = false;
            if (new_mode == MODE_MAMA_SUYU && this->current_mode_ != MODE_MAMA_SUYU && this->kettle_durumu_ == NORMAL &&
                this->ntc_sensor_ != nullptr && this->ntc_sensor_->state > MAMA_BASLAMAZ_T)
            {
                this->mama_reddet_(this->ntc_sensor_->state);
                new_mode = MODE_KAPALI;
                press_count = 0;
                mama_reddedildi = true;
                // Home Assistant'tan açılan anahtar kapalıya geri çekilir
                if (this->mama_suyu_switch_ && this->mama_suyu_switch_->state)
                    this->mama_suyu_switch_->publish_state(false);
            }

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
                    if (this->cay_demleme_select_ != nullptr && this->cay_demleme_select_->current_option() != "KAPALI")
                        this->cay_demleme_select_->publish_state("KAPALI");

                    publish_demleme_switch(false);
                }
                break;

            case MODE_FILTRE_KAHVE:
                this->reset_all_operations(false);
                if (this->filtre_kahve_switch_)
                    this->filtre_kahve_switch_->publish_state(false);
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
            if (new_mode != MODE_KAPALI)
            {
                this->mode_start_ms_ = millis(); // kendiliğinden kapanma bu andan sayılır
                this->brew_failed_ = false;      // yeni mod: önceki "demlenemedi" bilgisi silinir
            }

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
                this->mama_vurus_ms_ = 0;
                this->mama_onceki_vurus_ms_ = 0;
                this->mama_kazanc_ = MAMA_KAZANC_ILK;
                this->mama_otur_(MAMA_ILK_BEKLEME_MS); // ilk karar, okuma birkaç saniye izlendikten sonra
                this->mama_ilk_okuma_ = true;
                this->mama_hazir_oldu_ = false;
                if (this->mama_suyu_switch_)
                    this->mama_suyu_switch_->publish_state(true);

                break;

            case MODE_FILTRE_KAHVE:
            {
                // Çayla aynı düzenek; seviye yok. Su bitti algısı yoksa pompalama MAX'ın süresi kadar sürer.
                this->touch_states_[1] = true;
                this->set_demleme_suresi_for_level_(1);
                this->control_led(1);
                this->demleme_fb_.active = false;
                this->demleme_fb_end_ms_ = this->current_time_; // başlangıç konuşması tuş/komut bip'inin üstüne binmesin
                ESP_LOGI("CayseverRobotea", "Filtre kahve işlemi başlıyor.");
                this->cay_demleme_durumu_ = DEMLEME_HAZIRLIK;
                if (this->filtre_kahve_switch_)
                    this->filtre_kahve_switch_->publish_state(true);
                break;
            }

            case MODE_CAY_DEMLEME:
                this->touch_states_[3] = true;

                int level = press_count;
                if (level < 1)
                    level = 1;
                if (level > 4)
                    level = 4;

                // Süreyi burada ayarla (artık feedback fonksiyonu süre set etmiyor)
                this->set_demleme_suresi_for_level_(level);

                // Mod başlar başlamaz lamba kırmızı (ısıtma); seviye bildirimi bunun üstüne yürür
                this->control_led(3);

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

            // Kettle tabanda değilken (KORUMA) lambalar sönük kalır; geri konunca yeni modun lambası yakılır.
            // reset_all_operations tazelik lambalarını zaten söndürdü; dönüşte eski oturumun hâli geri yüklenmesin.
            if (this->kettle_durumu_ == KORUMA)
            {
                this->control_led(-1);
                bayled_previous_state = false;
                demled_previous_state = false;
            }

            // 4) Mod sensoru yayınla
            this->update_all_sensors();

            if (mama_reddedildi)
            {
                this->uyari_baslat_(0);
            }
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
