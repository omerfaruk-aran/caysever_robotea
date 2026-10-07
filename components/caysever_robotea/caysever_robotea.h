#pragma once
#include "esphome.h"
#include <string>
#include <functional>
#include <map>
#include <cstdint>
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <esphome/core/component.h>
#include <esphome/core/log.h>

// Bu sürümde demlemede "su bitti" algısı ve kendiliğinden kapanma var (sınama programı buna bakar).
#define CAYSEVER_ROBOTEA_SU_BITTI_ALGISI 1
#define CAYSEVER_ROBOTEA_SES_DENEME 1
#define CAYSEVER_ROBOTEA_DEMLENEMEDI 1
#define CAYSEVER_ROBOTEA_MAMA_FABRIKA 1
#define CAYSEVER_ROBOTEA_FILTRE_KAHVE 1

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
      MODE_KAPALI,
      MODE_SU_KAYNATMA,
      MODE_MAMA_SUYU,
      MODE_CAY_DEMLEME,
      MODE_FILTRE_KAHVE
    };

    class CayseverRobotea : public Component
    {
    public:
      void set_modlar(const Modlar &modlar) { this->modlar_ = modlar; }
      void set_mode_sensor(text_sensor::TextSensor *mode_sensor) { this->mode_sensor_ = mode_sensor; }
      void set_mode_state_sensor(text_sensor::TextSensor *mode_state_sensor) { this->mode_state_sensor_ = mode_state_sensor; }
      void set_kettle_state_sensor(text_sensor::TextSensor *kettle_state_sensor) { this->kettle_state_sensor_ = kettle_state_sensor; }
      void set_tazelik_sensor(text_sensor::TextSensor *tazelik_sensor) { this->tazelik_sensor_ = tazelik_sensor; }
      void set_tazelik_kalan_sensor(sensor::Sensor *tazelik_kalan_sensor) { this->tazelik_kalan_sensor_ = tazelik_kalan_sensor; }
      void set_mode(ActiveMode new_mode, int press_count);

      void set_ntc_sensor(sensor::Sensor *sensor) { this->ntc_sensor_ = sensor; }
      void set_su_kaynatma_switch(switch_::Switch *su_kaynatma_switch);
      void set_mama_suyu_switch(switch_::Switch *mama_suyu_switch);
      void set_filtre_kahve_switch(switch_::Switch *filtre_kahve_switch);
      void set_cay_demleme_select(select::Select *cay_demleme_select);
      void set_cay_demleme_max_switch(switch_::Switch *cay_demleme_max_switch);
      void set_buton_sesi_switch(switch_::Switch *buton_sesi_switch);
      void set_konusma_sesi_switch(switch_::Switch *konusma_sesi_switch);
      void set_su_kontrol_switch(switch_::Switch *su_kontrol_switch);
      void set_su_bitti_algisi_switch(switch_::Switch *sw) { this->su_bitti_algisi_switch_ = sw; }
      void set_demleme_hatti_sensor(sensor::Sensor *s) { this->demleme_hatti_sensor_ = s; }
      void set_otomatik_kapanma(uint32_t ms) { this->otomatik_kapanma_ms_ = ms; }
      void set_mama_suyu_sicak_tutma(uint32_t ms) { this->mama_sicak_tutma_ms_ = ms; }

      void handle_global_state_reset();
      void reset_all_operations(bool global_reset);
      void visual_feedback_demleme_level(int level); // Görsel geri bildirim

      // Tanılama: ses çipini doğrudan tetikler; "Buton Sesi" / "Konuşma Sesi" anahtarlarına bakmaz.
      // mask: bit0 = 1. ses pini (GPIO4), bit1 = 2. ses pini (GPIO19), bit2 = 3. ses pini (GPIO32).
      void ses_dene(uint8_t mask);

      void setup() override;
      void loop() override;

    protected:
      sensor::Sensor *ntc_sensor_ = nullptr; // NTC sensörü (ESPHome'dan bağlanacak)
      switch_::Switch *su_kaynatma_switch_ = nullptr;
      switch_::Switch *mama_suyu_switch_ = nullptr;
      switch_::Switch *filtre_kahve_switch_ = nullptr;
      switch_::Switch *buton_sesi_switch_ = nullptr;
      switch_::Switch *konusma_sesi_switch_ = nullptr;
      switch_::Switch *su_kontrol_switch_ = nullptr;

      select::Select *cay_demleme_select_ = nullptr;
      std::string cay_demleme_state_ = "KAPALI";

      switch_::Switch *cay_demleme_max_switch_ = nullptr;
      bool suppress_cay_demleme_max_cb_{false};

      Modlar modlar_; // Modlar struct'ı burada saklanacak
      ActiveMode current_mode_{MODE_KAPALI};
      text_sensor::TextSensor *mode_sensor_{nullptr};
      text_sensor::TextSensor *mode_state_sensor_{nullptr};
      text_sensor::TextSensor *kettle_state_sensor_{nullptr};
      text_sensor::TextSensor *tazelik_sensor_{nullptr};  // Yok / Demleniyor / Taze / Bayat
      sensor::Sensor *tazelik_kalan_sensor_{nullptr};     // Taze kalma süresinden kalan dakika
      int tazelik_son_durum_{-1};                         // Son yayınlanan durum (0=Yok 1=Demleniyor 2=Taze 3=Bayat)
      int tazelik_son_kalan_{-2};                         // Son yayınlanan kalan dakika (-1 = NAN)
      const char *active_mode_to_string(ActiveMode mode);

      SuKaynatmaDurumu su_kaynatma_durumu_;
      MamaSuyuDurumu mama_suyu_durumu_;
      CayDemlemeDurumu cay_demleme_durumu_;
      KettleDurumu kettle_durumu_ = NORMAL;
      KettleDurumu previous_mode_ = NORMAL;

      bool pending_mode_change_{false};
      ActiveMode pending_mode_{MODE_KAPALI};
      int pending_press_count_{0};
      bool pending_process_scheduled_{false};

      void apply_mode_(ActiveMode new_mode, int press_count);
      void schedule_process_pending_();
      void process_pending_();

      void publish_mode_();
      void publish_kettle_state_();
      void publish_mode_state_();
      void publish_tazelik_();

      void update_su_kaynatma(bool su_kaynatma);
      void update_mama_suyu(bool mama_suyu);
      void update_filtre_kahve(bool filtre_kahve);
      void update_cay_demleme(const std::string &level);
      void update_all_sensors();
      void handle_critical_sounds();

      void led_blink(int pin, int times, int delay_ms);
      void on_wifi_connected();    // Wi-Fi bağlantısı sağlandığında
      void on_wifi_disconnected(); // Wi-Fi bağlantısı kesildiğinde
      void handle_touch_input();
      void handle_touch_input_food_water();
      void handle_touch_input_boiling_water();
      void handle_touch_input_brew_tea();
      void handle_touch_input_filter_coffee();
      void handle_touch_input_toggle_button_sound();
      void handle_touch_input_toggle_speak_sound();
      void check_water_level();
      bool slope_is_sustained_();           // Su seviye eğimi: artış okumadan okumaya sürüyor mu (tek okuma sıçraması değil mi)
      void record_ntc_sample_(float value); // NTC'nin her yeni okumasını zamanıyla sakla
      // KRITIK'e neden girildiği. Su yetersizliği (eğim ya da sabit sınır) doğrulandıysa fabrika yazılımındaki gibi
      // "su ekleyin" klibi de çalınır; diğer sebeplerde yalnız alarm.
      enum KritikSebep : uint8_t
      {
        KRITIK_SEBEP_ASIRI_ISINMA, // 120 °C kesmesi
        KRITIK_SEBEP_SU_AZ         // check_water_level(): kettle'da su yok ya da çok az
      };
      void enter_critical_(KritikSebep sebep = KRITIK_SEBEP_ASIRI_ISINMA); // KRITIK'e geçiş: röleler, işlemler ve mod kapanır, alarm başlar
      void restore_mode_leds_();            // Aktif moda ve aşamasına göre tuş LED'ini geri yak
      void handle_critical_mode_leds();
      void handle_exit_critical_mode();
      void control_led(int button_index, bool is_white = false);  // LED kontrol fonksiyonu
      void activate_sound(const std::map<int, bool> &pin_states); // Ses kontrol fonksiyonu
      void play_button_sound();
      void play_mama_suyu_hazir_sound();
      void play_cay_demleme_start_sound();
      void play_cay_demleme_done_sound();
      void play_filtre_kahve_hazirlaniyor_sound();
      void play_su_ekle_sound(); // "hazneye su ekle..." uyarısı (fabrika yazılımı hata durumlarında çalar)
      void play_su_kaynadi_sound();

      void handle_mama_suyu_hazirla(); // Mama suyu hazırlama fonksiyonu
      void handle_su_kaynatma();       // Su kaynatma işlemini yönetecek fonksiyon
      void handle_cay_demleme();       // Çay demleme işlemi (su kaynatma + demleme)
      void maintain_temperature(float min, float max);

      // ---(kaynama sonrası cam demliği ısıtma) ---
      uint32_t su_kaynatma_boil_ms_{0};                     // kaynama görüldüğü an
      static constexpr uint32_t STEAM_BOOST_MS = 20 * 1000; // 20sn (0-60 arası güvenli)
      static constexpr float STEAM_BOOST_MAX_T = 103.0f;    // taban sıcaklığı tavanı (çok önemli)
      static constexpr float OVERHEAT_CUTOFF_T = 120.0f;    // fail-safe (bu mutlaka olmalı)
      static constexpr float WL_STATIC_LIMIT_T = 115.0f;    // "su yok" sabit sınırı (fabrika yazılımındaki değer)
      bool steam_boost_enabled_{false};                     // bu kaynatmada boost var mı?
      static constexpr float STEAM_BOOST_START_T = 80.0f;   // başlarken NTC < 80 ise boost aktif

      // Su seviyesi kontrolü için değişkenler
      uint32_t wl_grace_until_{0};  // Kettle yerine konduktan sonra geçici süre
      uint32_t wl_win_start_ms_{0}; // Isı ölçümü için başlangıç zamanı
      float wl_win_start_t_{0};     // Başlangıç sıcaklığı
      uint8_t wl_susp_{0};          // Şüphe sayacı (0..3)

      // Eğim kontrolünün doğrulaması: uç-nokta eğimi eşiği aşsa bile artışın sürekli olması aranır
      static constexpr uint8_t WL_MIN_INTERVALS = 3;         // bundan az okuma aralığı varsa doğrulama yapılamaz (eski davranış)
      static constexpr float WL_SUSTAIN_MIN_MEDIAN = 1.0f;   // aralık eğimlerinin ortancası en az bu olmalı (°C/sn)
      static constexpr float WL_GLITCH_DROP = -1.0f;         // ısıtıcı açıkken bundan hızlı düşüş (°C/sn) bozuk okumadır
      struct NtcSample
      {
        uint32_t ms;
        float t;
      };
      static constexpr uint8_t NTC_SAMPLE_COUNT = 16;
      NtcSample ntc_samples_[NTC_SAMPLE_COUNT]{}; // halka tampon: son okumalar
      uint8_t ntc_sample_head_{0};                // bir sonraki okumanın yazılacağı yer
      uint8_t ntc_sample_len_{0};

      // Kettle'ın tabandan ayrı kaldığı süre. KRITIK'teyken en az KRITIK_ONAY_MS kaldırılıp geri konursa alarm
      // onaylanmış sayılır; daha kısası (tek okumalık sensör kaybı) KRITIK'i bozmaz.
      uint32_t koruma_start_ms_{0};
      static constexpr uint32_t KRITIK_ONAY_MS = 3000;

      // KRITIK alarmı saniyede bir bip verir. Su yetersizliğinde "Konuşma Sesi" açıksa önce "su ekleyin" klibi
      // çalınır: klip, girişteki bip'lerden KRITIK_SU_EKLE_KLIP_MS sonra tetiklenir ve alarmın ilk bip'i
      // KRITIK_SU_EKLE_ALARM_MS'ye ertelenir (her tetik çipte çalan klibi keser). Klip gerçek cihazda 4-5 sn sürüyor
      // (kulakla ölçüldü); alarm klibin başlamasından 6,4 sn sonra gelir. Röleler girişte hemen kapanır; ertelenen
      // yalnız sestir.
      static constexpr uint32_t KRITIK_ALARM_ARALIK_MS = 1000;
      static constexpr uint32_t KRITIK_SU_EKLE_KLIP_MS = 600;
      static constexpr uint32_t KRITIK_SU_EKLE_ALARM_MS = 7000;
      uint32_t kritik_alarm_bekleme_ms_{KRITIK_ALARM_ARALIK_MS}; // bir sonraki alarm bip'ine kadar beklenecek süre

      // Röle ve sensör pinleri
      int relay_pin_ = 17;         // Su kaynatma rölesi GPIO17
      int demleme_relay_pin_ = 18; // Demleme rölesi GPIO18
      unsigned long current_time_;

      // --- Demlemede "su bitti" algısı (GPIO34) -------------------------------------------------------------
      // Fabrika yazılımı demlemeyi süreyle bitirmez: demleme rölesini 10 sn açık tutar, kısa bir an bırakır ve bu
      // girişte şebeke kenarı kalıp kalmadığına bakar. Röle bırakılmışken kenar varsa demleme hattı (ısıtıcı ve
      // kendi termostatı) sağlamdır, yani üst haznede su vardır; kenar yoksa su bitmiş, termostat açmıştır.
      // Cihazda ölçülen: boşta 120-150 kenar/sn, röle açıkken 0, kuruda ~40 sn çalışınca işaret kesiliyor ve
      // dakikalar sonra geri geliyor; işaret kettle'ın tabanda olup olmamasından bağımsız.
      // Bu düzen yalnız yaml'da su_bitti_algisi_switch verilmişse, o anahtar açıksa ve cihaz açıldığından beri
      // girişte kenar görülmüşse kullanılır; aksi hâlde demleme eskisi gibi seçilen seviyenin süresiyle yürür.
      int brew_sense_pin_ = 34;
      switch_::Switch *su_bitti_algisi_switch_ = nullptr;
      sensor::Sensor *demleme_hatti_sensor_ = nullptr; // tanılama: girişte görülen kenar sayısı (kenar/sn)
      static volatile uint32_t brew_sense_edges_;      // kesmede artar
      static void brew_sense_isr_();

      enum BrewPhase : uint8_t
      {
        BREW_TIMED,   // eski düzen: seviye süresi kadar röle açık + 240 sn bekleme
        BREW_PUSH,    // ilk itiş: röle kesintisiz açık
        BREW_ON,      // döngü: röle açık
        BREW_SENSE,   // döngü: röle bırakıldı, kenar sayılıyor
        BREW_STEEP    // su bitti: demlenme bekleniyor
      };
      BrewPhase brew_phase_{BREW_TIMED};
      uint32_t brew_phase_start_ms_{0};
      uint32_t brew_cycle_start_ms_{0}; // ilk ölçümün başladığı an ("erken bitti" bu andan sayılır)
      uint32_t brew_last_on_ms_{0};     // rölenin son açıldığı an (demlenme süresi bu andan sayılır)
      uint32_t brew_pump_ms_{0};        // rölenin bu demlemede toplam açık kaldığı süre (üst sınır için)
      uint32_t brew_sense_base_{0};     // ölçüm penceresi başındaki kenar sayacı
      bool brew_sense_base_taken_{false};
      bool brew_cycle_started_{false};
      bool brew_sense_trusted_{false};  // açılıştan beri röle bırakılmışken girişte kenar görüldü
      bool brew_sense_fault_{false};    // girişte anlamsız hızda kenar görüldü; algı bu açılış için devre dışı
      uint32_t brew_win_start_ms_{0};   // tanılama/güven penceresi (1 sn)
      uint32_t brew_win_base_{0};
      bool brew_win_clean_{false};      // pencere boyunca röle bırakılmış ve kettle yerindeydi
      bool brew_win_open_{false};
      bool brew_win_relay_off_{false};  // pencere boyunca röle bırakılmıştı (kettle yerinde olmasa da)
      int brew_rate_published_{-1};     // tanılama sensöründe son yayınlanan değer (-1 = henüz yok)

      static constexpr uint32_t BREW_PUSH_MS = 16000;        // fabrika: ilk 16 sn kesintisiz
      static constexpr uint32_t BREW_ON_MS = 10000;          // fabrika: her döngüde 10 sn açık
      static constexpr uint32_t BREW_SENSE_SETTLE_MS = 40;   // röle kontağının bırakması için
      static constexpr uint32_t BREW_SENSE_WINDOW_MS = 200;  // ölçüm penceresi (fabrika ~150 ms)
      static constexpr uint32_t BREW_SENSE_MIN_EDGES = 6;    // fabrika eşiği: bundan az kenar = su bitti
      static constexpr uint32_t BREW_EARLY_MS = 60000;       // döngünün ilk 60 sn'sinde biterse demleme yapılamadı sayılır (hata)
      static constexpr uint32_t KAHVE_DEMLENME_MS = 120000;  // filtre kahve; fabrika: son röle açılışından 120 s
      static constexpr uint32_t KAHVE_TAZELIK_MS = 40 * 60 * 1000; // filtre kahve; fabrika: 2400 s
      static constexpr uint32_t BREW_STEEP_MS = 900000;      // fabrika: son röle açılışından 900 sn sonra çay hazır
      static constexpr uint32_t BREW_TRUST_MIN_RATE = 30;    // kenar/sn: güven için en az (6 kenar / 200 ms'nin karşılığı)
      static constexpr uint32_t BREW_STORM_RATE = 2000;      // kenar/sn: bunun üstü şebeke işareti olamaz

      bool brew_sense_usable_() const;
      void update_brew_sense_();
      void handle_brew_cycle_();
      void brew_set_relay_(bool on);
      void brew_resume_after_koruma_();
      void brew_fail_();                  // demleme yapılamadı: her şey kapanır, uyarı verilir
      // Uyarı: verilen tuşun lambası üç kez kırmızı yanıp söner; ses olarak üç bip ya da (su_ekle ve konuşma açıksa)
      // "hazneye su ekle..." klibi çalar.
      void uyari_baslat_(int led, bool su_ekle = false);
      void uyari_adimi_(bool on);         // uyarının bir adımı: bip ve lamba
      int uyari_led_{-1};
      bool uyari_su_ekle_{false};
      uint8_t uyari_adim_no_{0};

      // --- Mama suyu: 40 °C'ye "vur, bekle, ölç" ile yaklaşılır ---
      // Sensör kettle tabanındadır ve ısıtıcının 15–20 sn gerisinden gelir; ısıtıcı röleyle tam güçte çalıştığı için
      // okumaya bakarak kesmek suyu hedefin üstüne taşırır (gerçek cihazda ölçüldü: 46,6 °C'de kesilince su 44–52 °C).
      // Bu yüzden kısa bir vuruş yapılır, okuma oturana kadar beklenir, vuruşun suyu kaç derece ısıttığı ölçülür ve
      // sonraki vuruş buna göre boyutlanır. "Hazır" yalnız oturmuş okuma hedef bandındayken söylenir.
      static constexpr float MAMA_HEDEF_T = 40.0f;      // tuşun üstünde yazan sıcaklık
      static constexpr float MAMA_HAZIR_ALT_T = 39.0f;  // oturmuş okuma bu ikisinin arasındaysa "mama suyu hazır"
      static constexpr float MAMA_HAZIR_UST_T = 41.5f;  // üstündeyse soğuması beklenir, "hazır" denmez
      static constexpr float MAMA_TUT_T = 38.0f;        // hazırken bunun altına inince ısıtılır
      static constexpr float MAMA_YENIDEN_T = 35.0f;    // hazırken bunun altına inerse baştan ısıtılır, yeniden haber verilir
      static constexpr float MAMA_BASLAMAZ_T = 45.0f;   // bundan sıcak suyla mod başlamaz (fabrika yazılımında 44 °C)
      static constexpr uint32_t MAMA_OTURMA_MS = 40000; // vuruştan sonra okumanın oturması için bekleme
      static constexpr uint32_t MAMA_ILK_BEKLEME_MS = 6000;
      static constexpr float MAMA_VURUS_MIN_SN = 1.0f;
      static constexpr float MAMA_VURUS_MAX_SN = 12.0f;
      static constexpr float MAMA_KAZANC_ILK = 2.5f;    // °C / vuruş saniyesi; ilk vuruş az su varmış gibi boyutlanır
      enum MamaFaz : uint8_t
      {
        MAMA_FAZ_OLC,   // okuma oturdu: karar ver
        MAMA_FAZ_VURUS, // ısıtıcı açık
        MAMA_FAZ_OTUR   // ısıtıcı kapalı, okumanın oturması bekleniyor
      };
      MamaFaz mama_faz_{MAMA_FAZ_OTUR};
      uint32_t mama_faz_ms_{0};         // fazın başladığı an
      uint32_t mama_bekleme_ms_{0};     // OTUR fazının süresi
      uint32_t mama_vurus_ms_{0};       // süren / son vuruşun süresi
      uint32_t mama_onceki_vurus_ms_{0}; // bir önceki vuruşun süresi (büyüme sınırı için; 0 = bu ısıtmanın ilk vuruşu)
      float mama_vurus_oncesi_t_{0};    // son vuruştan önceki oturmuş okuma
      float mama_kazanc_{MAMA_KAZANC_ILK}; // ölçülen °C / vuruş saniyesi
      bool mama_ilk_okuma_{false};      // mod başladıktan sonraki ilk okuma daha değerlendirilmedi
      bool mama_hazir_oldu_{false};     // bu modda en az bir kez "hazır" denildi
      uint32_t mama_hazir_ms_{0};       // ilk "hazır" anı
      uint32_t mama_sicak_tutma_ms_{0}; // yaml: hazır olduktan sonra mod bu kadar açık kalır (0 = ayrı sınır yok)
      void mama_isitici_(bool on);
      void mama_vurus_basla_(float t, float hedef);
      void mama_otur_(uint32_t bekleme_ms);
      void mama_reddet_(float t);
      bool brew_failed_{false};           // son demleme yapılamadı; yeni bir mod başlatılana kadar tazelik sensöründe görünür
      void publish_brew_rate_(uint32_t rate, bool force = false);
      void finish_demleme_();

      // Kendiliğinden kapanma (fabrika: mod açıldıktan 2 saat sonra). 0 = kapalı (eski davranış).
      uint32_t otomatik_kapanma_ms_{0};
      uint32_t mode_start_ms_{0};
      void check_auto_off_();

      unsigned long demleme_start_time_ = 0; // Demleme işlemi başlangıç zamanı
      unsigned long demleme_end_time_ = 0;   // Demleme işlemi bitiş zamanı
      unsigned long demled_start_time_ = 0;  // DemLED başlangıç zamanı
      bool demled_active_ = false;           // DemLED aktif mi?
      static constexpr uint32_t TAZELIK_SURESI_MS = 60 * 60 * 1000; // Dem sonrası çayın taze sayıldığı süre (60 dk)
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

      struct DemlemeFeedbackState
      {
        bool active{false};
        int level{0};          // 1..4 (1=MAX)
        int blink_done{0};     // kaç blink tamam
        bool phase_on{false};  // white ON/OFF fazı
        uint32_t next_ms{0};   // bir sonraki adım zamanı
        bool post_wait{false}; // blink bittikten sonra 500ms bekleme
      } demleme_fb_;

      uint32_t demleme_fb_end_ms_{0};                      // seviye geri bildiriminin (son bip'in) bittiği an
      static constexpr uint32_t DEMLEME_FB_GAP_MS = 600;   // bip ile demleme başlangıç konuşması arasında bırakılan süre

      void start_demleme_feedback_(int level);
      void process_demleme_feedback_();
      void set_demleme_suresi_for_level_(int level);

      uint32_t sound_pulse_token_{0};
      // Ses çipinin tetik darbesi. Fabrika yazılımı pinleri 50 ms tutuyor. 10 ms'lik darbeyle bir cihazda
      // kaynatma bitimindeki "su kaynadı" tetiği verildiği hâlde çip konuşmadı (tetik, ısıtıcı rölesinin
      // bırakıldığı ana denk geliyor); süre fabrika değerine çekildi.
      static constexpr uint32_t SOUND_PULSE_MS = 50;

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

      // Filtre kahve, çay demlemeyle aynı düzeneği kullanır (fabrika yazılımındaki gibi); süreler, lamba ve başlangıç sesi farklıdır.
      bool kahve_() const { return this->current_mode_ == MODE_FILTRE_KAHVE; }
      int demleme_led_() const { return this->kahve_() ? 1 : 3; }
      uint32_t demlenme_ms_() const { return this->kahve_() ? KAHVE_DEMLENME_MS : BREW_STEEP_MS; }
      uint32_t tazelik_ms_() const { return this->kahve_() ? KAHVE_TAZELIK_MS : TAZELIK_SURESI_MS; }

      void on_filtre_kahve_change(bool state)
      {
        if (this->touch_states_[1] == state)
        {
          ESP_LOGI("CayseverRobotea", "Durum zaten %s, işlem yapılmadı.",
                   state ? "aktif" : "pasif");
          return;
        }

        ESP_LOGI("CayseverRobotea", "Filtre Kahve Switch durumu değişti: %s", state ? "ON" : "OFF");

        this->play_button_sound();
        this->update_filtre_kahve(state);
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

      void on_cay_demleme_max_change(bool state)
      {
        if (this->suppress_cay_demleme_max_cb_)
          return;

        ESP_LOGI("CayseverRobotea", "Çay Demleme (MAX) switch: %s", state ? "ON" : "OFF");

        this->play_button_sound();

        if (state)
        {
          // ON -> her zaman MAX ile başlat
          this->set_mode(MODE_CAY_DEMLEME, 1); // 1 => MAX
        }
        else
        {
          // OFF -> sadece demleme modundaysa kapat
          if (this->current_mode_ == MODE_CAY_DEMLEME)
            this->set_mode(MODE_KAPALI, 0);
        }
      }
    };

  } // namespace caysever_robotea
} // namespace esphome
