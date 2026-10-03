// caysever_robotea bileşeninin GERÇEK kodunu bilgisayarda çalıştıran sınama programı.
// Pinler, saat ve ESPHome sınıfları stubs/ altında taklit edilir; bileşenin kendisi değiştirilmeden derlenir.
//
// Bileşende fonksiyon içi static değişkenler olduğu için HER SENARYO AYRI SÜREÇTE çalıştırılmalıdır:
//   ./robotea_test <senaryo> [-v]        (hepsi için: ./run.sh)
// Çıkış kodu: 0 = bütün beklentiler tuttu, 1 = en az biri tutmadı.

#include "caysever_robotea.h"
#include <WiFi.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace esphome;
using namespace esphome::caysever_robotea;

namespace
{
  const int RELAY = 17;     // su kaynatma (ısıtıcı) rölesi
  const int DEM_RELAY = 18; // demleme rölesi
  const int BAY_LED = 22;
  const int DEM_LED = 21;
  const int BTN_LEDS[5] = {15, 25, 13, 5, 26};
  const int TOUCH[4] = {12, 14, 27, 33}; // 0=mama suyu, 2=su kaynatma, 3=çay demleme
  const int SOUND[3] = {4, 19, 32};
  const uint32_t STEP_MS = 20;           // ESPHome döngü aralığına yakın
  const uint32_t SAMPLE_MS = 2000;       // yaml'daki adc update_interval

  int g_failed = 0;
  int g_checked = 0;

  void check(bool ok, const std::string &what)
  {
    g_checked++;
    if (!ok)
      g_failed++;
    printf("  [%s] %s\n", ok ? "TAMAM" : "TUTMADI", what.c_str());
  }

  std::string hms(uint32_t ms, int base_s = 0)
  {
    int s = base_s + ms / 1000;
    char b[16];
    snprintf(b, sizeof b, "%02d:%02d:%02d", (s / 3600) % 24, (s / 60) % 60, s % 60);
    return b;
  }

  // Korumalı üyeleri sınama için görünür yapar; davranış eklemez.
  struct Exposed : CayseverRobotea
  {
    using CayseverRobotea::cay_demleme_durumu_;
    using CayseverRobotea::current_mode_;
    using CayseverRobotea::kettle_durumu_;
    using CayseverRobotea::kritik_sound_active_;
    using CayseverRobotea::mama_suyu_durumu_;
    using CayseverRobotea::relay_active_;
    using CayseverRobotea::su_kaynatma_durumu_;
  };

  struct Rig
  {
    Exposed dev;
    sensor::Sensor ntc, tazelik_kalan;
    switch_::Switch su_kaynatma, mama_suyu, buton_sesi, konusma_sesi, su_kontrol;
    std::vector<std::pair<uint32_t, std::string>> sounds;    // ses çipine giden tetikler: (an, pinler)
    std::string last_sound_pat;
    select::Select cay;
    text_sensor::TextSensor aktif_mod, mod_durumu, kettle_durumu, tazelik;

    long steps = 0;
    long violations = 0;         // "NORMAL değilken röle açık" sayısı (her döngü sonunda bakılır)
    uint32_t relay_on_ms = 0;    // ısıtıcı rölesinin toplam açık kaldığı süre
    uint32_t dem_relay_on_ms = 0; // demleme rölesinin toplam açık kaldığı süre
    int dem_relay_on_count = 0;  // demleme rölesinin kaç kez çekildiği
    int last_dem_relay = LOW;
    uint32_t first_kritik_ms = 0; // ilk KRITIK anı (0 = hiç)
    int sound_pulses = 0;        // ses çipine giden tetik sayısı (GPIO4 yükselen kenar)
    int last_sound_pin = LOW;

    explicit Rig(bool with_select = true, bool su_kontrol_on = true)
    {
      for (int p : TOUCH)
        hoststub::st().pin_level[p] = HIGH; // tuşlar bırakılmış (basılı = LOW)
      dev.set_ntc_sensor(&ntc);
      dev.set_su_kaynatma_switch(&su_kaynatma);
      dev.set_mama_suyu_switch(&mama_suyu);
      if (with_select)
        dev.set_cay_demleme_select(&cay);
      dev.set_buton_sesi_switch(&buton_sesi);
      dev.set_konusma_sesi_switch(&konusma_sesi);
      dev.set_su_kontrol_switch(&su_kontrol);
      dev.set_mode_sensor(&aktif_mod);
      dev.set_mode_state_sensor(&mod_durumu);
      dev.set_kettle_state_sensor(&kettle_durumu);
      dev.set_tazelik_sensor(&tazelik);
      dev.set_tazelik_kalan_sensor(&tazelik_kalan);
      // yaml: üç ayar anahtarı RESTORE_DEFAULT_ON
      buton_sesi.publish_state(true);
      konusma_sesi.publish_state(true);
      su_kontrol.publish_state(su_kontrol_on);
      dev.setup();
    }

    void step(uint32_t ms = STEP_MS)
    {
      hoststub::st().now_ms += ms;
      dev.host_run_scheduler();
      dev.loop();
      steps++;
      {
        std::string pat;
        for (int sp_pin : SOUND)
          if (digitalRead(sp_pin) == HIGH)
            pat += (pat.empty() ? "" : "+") + std::to_string(sp_pin);
        if (!pat.empty() && pat != last_sound_pat)
          sounds.emplace_back(millis(), pat);
        last_sound_pat = pat;
      }
      if (digitalRead(RELAY) == HIGH)
        relay_on_ms += ms;
      {
        int dr = digitalRead(DEM_RELAY);
        if (dr == HIGH)
          dem_relay_on_ms += ms;
        if (dr == HIGH && last_dem_relay == LOW)
          dem_relay_on_count++;
        last_dem_relay = dr;
      }
      if (dev.kettle_durumu_ != NORMAL && (digitalRead(RELAY) != LOW || digitalRead(DEM_RELAY) != LOW))
        violations++;
      if (dev.kettle_durumu_ == KRITIK && first_kritik_ms == 0)
        first_kritik_ms = millis();
      int sp = digitalRead(4);
      if (sp == HIGH && last_sound_pin == LOW)
        sound_pulses++;
      last_sound_pin = sp;
    }
    void run(uint32_t ms)
    {
      for (uint32_t t = 0; t < ms; t += STEP_MS)
        step();
    }
    void feed(float t) { ntc.publish_state(t); }
    // Sabit sıcaklıkla bekle: 2 sn'de bir aynı değer okunur.
    void hold(float t, uint32_t ms)
    {
      for (uint32_t el = 0; el < ms; el += SAMPLE_MS)
      {
        feed(t);
        run(SAMPLE_MS);
      }
    }
    void press(int touch_index, uint32_t hold_ms = 200)
    {
      hoststub::st().pin_level[TOUCH[touch_index]] = LOW;
      run(hold_ms);
      hoststub::st().pin_level[TOUCH[touch_index]] = HIGH;
      run(100);
    }
    bool btn_leds_all_off() const
    {
      for (int p : BTN_LEDS)
        if (digitalRead(p) != LOW)
          return false;
      return true;
    }
    std::string btn_leds() const
    {
      std::string s;
      for (int p : BTN_LEDS)
        s += digitalRead(p) ? '1' : '0';
      return s;
    }
    bool relays_off() const { return digitalRead(RELAY) == LOW && digitalRead(DEM_RELAY) == LOW; }
  };

  // Basit ısıl model (kapalı çevrim senaryolar için): röle açıkken sabit hızla ısınır, kapalıyken yavaş soğur.
  // Su varsa sıcaklık kaynama noktasında durur; "kuru" modelde durmaz.
  struct Thermal
  {
    float t;
    float heat;      // °C/sn, röle açıkken
    float cool;      // °C/sn, röle kapalıyken
    bool boils;      // su var: ~100 °C'de durur
    float boil_cap = 100.6f;

    void advance(bool relay_on, float dt_s)
    {
      if (relay_on)
        t += heat * dt_s;
      else
        t -= cool * dt_s;
      if (boils && t > boil_cap)
        t = boil_cap;
      if (t < 20.0f)
        t = 20.0f;
    }
  };

  // Kapalı çevrim çalıştır: her 2 sn'de modelden okuma al, yayınla. glitch(i, t) okumayı bozabilir.
  void run_thermal(Rig &rig, Thermal &th, uint32_t ms, const std::function<float(int, float)> &glitch = nullptr,
                   const std::function<bool()> &stop = nullptr)
  {
    int i = 0;
    for (uint32_t el = 0; el < ms; el += SAMPLE_MS, i++)
    {
      float v = th.t;
      if (glitch)
        v = glitch(i, v);
      rig.feed(v);
      for (uint32_t s = 0; s < SAMPLE_MS; s += STEP_MS)
      {
        rig.step();
        th.advance(digitalRead(RELAY) == HIGH, STEP_MS / 1000.0f);
      }
      if (stop && stop())
        return;
    }
  }

  // ---------------------------------------------------------------------------------------------
  // Gerçek gecenin tekrar oynatılması
  // ---------------------------------------------------------------------------------------------
  struct Row
  {
    uint32_t t_ms;
    std::string clock, type, value;
  };

  std::vector<Row> load_csv(const std::string &path)
  {
    std::vector<Row> rows;
    std::ifstream f(path);
    if (!f)
    {
      printf("  veri dosyası açılamadı: %s\n", path.c_str());
      exit(2);
    }
    std::string line;
    while (std::getline(f, line))
    {
      if (line.empty() || line[0] == '#' || line.rfind("t_ms", 0) == 0)
        continue;
      std::stringstream ss(line);
      Row r;
      std::string t;
      std::getline(ss, t, ',');
      std::getline(ss, r.clock, ',');
      std::getline(ss, r.type, ',');
      std::getline(ss, r.value);
      r.t_ms = (uint32_t)std::stoul(t);
      rows.push_back(r);
    }
    return rows;
  }

  // Dönüş: KRITIK'e geçilen an (ms), geçilmediyse 0. Gerçek cihazın yayınladığı durumlarla karşılaştırır.
  uint32_t replay(Rig &rig, const std::string &path, int base_s, uint32_t until_ms, int &matched, int &expected,
                  std::vector<std::string> &misses, bool stop_at_kritik = true)
  {
    auto rows = load_csv(path);
    size_t i = 0;
    std::vector<Row> exp;
    uint32_t end = rows.empty() ? 0 : std::min(until_ms, rows.back().t_ms + 4000);
    while (millis() < end)
    {
      while (i < rows.size() && rows[i].t_ms <= millis())
      {
        const Row &r = rows[i++];
        if (r.type == "T" || r.type == "t")
          rig.feed(std::stof(r.value));
        else if (r.type == "CMD")
        {
          if (r.value == "su_kaynatma=on")
            rig.su_kaynatma.publish_state(true);
          else if (r.value == "su_kaynatma=off")
            rig.su_kaynatma.publish_state(false);
          else if (r.value.rfind("cay=", 0) == 0)
            rig.cay.publish_state(r.value.substr(4));
        }
        else if (r.type == "EXP")
          exp.push_back(r);
      }
      rig.step();
      if (stop_at_kritik && rig.first_kritik_ms != 0)
        break;
    }
    // Karşılaştırma: gerçekte yayınlanan her durum, simülasyonda ±6 sn içinde aynı değerle görülmeli.
    auto find = [&](text_sensor::TextSensor &ts, const std::string &val, uint32_t t) {
      for (auto &c : ts.changes)
        if (c.second == val && (c.first > t ? c.first - t : t - c.first) <= 6000)
          return true;
      return false;
    };
    // Gerçek cihaz o gece KRITIK'e geçtiyse, o andan sonraki yayınlar alarmın sonucudur; karşılaştırılmaz.
    uint32_t real_kritik_ms = 0xFFFFFFFF;
    for (auto &e : exp)
      if (e.value == "kettle_durumu=KRITIK")
      {
        real_kritik_ms = e.t_ms;
        printf("  bilgi: gerçek cihaz bu veride %s'de KRITIK'e geçmişti\n", e.clock.c_str());
        break;
      }
    for (auto &e : exp)
    {
      if (e.t_ms > millis() || e.t_ms + 1500 >= real_kritik_ms)
        continue; // KRITIK'te durdurulduysa ya da gerçek alarmın sonucuysa karşılaştırılmaz
      auto eq = e.value.find('=');
      std::string name = e.value.substr(0, eq), val = e.value.substr(eq + 1);
      text_sensor::TextSensor *ts = name == "mod_durumu" ? &rig.mod_durumu : name == "kettle_durumu" ? &rig.kettle_durumu
                                                                          : name == "cay_tazeligi"   ? &rig.tazelik
                                                                          : name == "aktif_mod"      ? &rig.aktif_mod
                                                                                                     : nullptr;
      if (ts == nullptr)
        continue;
      if (name == "kettle_durumu" && val == "KRITIK")
        continue; // ayrıca raporlanıyor
      expected++;
      if (find(*ts, val, e.t_ms))
        matched++;
      else
        misses.push_back(e.clock + " " + e.value);
    }
    (void)base_s;
    return rig.first_kritik_ms;
  }

  int scenario_replay_aksam()
  {
    // 2 Eki 2026 17:45 → 21:52: iki su kaynatma, yarıda kesilen demleme, MAX demleme → Taze → 21:50:18'de tek
    // bozuk okuma (84.37 °C). Fabrika öncesi kod 21:50:26'da yanlış KRITIK verdi.
    const int base = 17 * 3600 + 45 * 60;
    Rig rig;
    int matched = 0, expected = 0;
    std::vector<std::string> misses;
    uint32_t k = replay(rig, "data/2026-10-02-aksam.csv", base, 0xFFFFFFFF, matched, expected, misses);
    printf("  ölçüm: KRITIK %s · gerçek cihazla eşleşen durum geçişi %d/%d · röle toplam açık %.0f sn · ihlal %ld\n",
           k ? ("@ " + hms(k, base)).c_str() : "yok", matched, expected, rig.relay_on_ms / 1000.0, rig.violations);
    for (auto &m : misses)
      printf("    eşleşmeyen: %s\n", m.c_str());
    check(k == 0, "gerçek gecenin verisinde yanlış KRITIK alarmı yok");
    check(matched == expected, "cihazın o gece yayınladığı bütün durum geçişleri aynı anda yeniden üretildi");
    check(rig.violations == 0, "NORMAL dışındaki hiçbir anda röle açık kalmadı");
    return 0;
  }

  int scenario_replay_yeniden()
  {
    // 21:53:59 yeniden başlama → MAX demleme → 22:05:54 Taze → 23:05:54 Bayat.
    const int base = 21 * 3600 + 53 * 60 + 59;
    Rig rig;
    int matched = 0, expected = 0;
    std::vector<std::string> misses;
    uint32_t k = replay(rig, "data/2026-10-02-yeniden.csv", base, 0xFFFFFFFF, matched, expected, misses);
    printf("  ölçüm: KRITIK %s · eşleşen durum geçişi %d/%d · tazelik son: %s · ihlal %ld\n",
           k ? ("@ " + hms(k, base)).c_str() : "yok", matched, expected, rig.tazelik.state.c_str(), rig.violations);
    for (auto &m : misses)
      printf("    eşleşmeyen: %s\n", m.c_str());
    check(k == 0, "KRITIK yok");
    check(matched == expected, "bütün durum geçişleri (Taze ve 60 dk sonra Bayat dâhil) yeniden üretildi");
    check(rig.violations == 0, "NORMAL dışındaki hiçbir anda röle açık kalmadı");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Su azlığı algılaması: gerçek tehlike hâlâ yakalanıyor mu?
  // ---------------------------------------------------------------------------------------------
  int scenario_su_azligi(float rate, bool boils, bool expect_kritik, uint32_t max_delay_ms, const char *what)
  {
    Rig rig;
    Thermal th{25.0f, rate, 0.02f, boils};
    rig.hold(25.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    uint32_t start = millis();
    run_thermal(rig, th, 240000, nullptr, [&] { return rig.first_kritik_ms != 0 || rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA; });
    uint32_t dly = rig.first_kritik_ms ? rig.first_kritik_ms - start : 0;
    printf("  ölçüm: %.2f °C/sn → KRITIK %s · o andaki sıcaklık %.1f °C · ihlal %ld\n", rate,
           rig.first_kritik_ms ? ("başlangıçtan " + std::to_string(dly / 1000.0).substr(0, 4) + " sn sonra").c_str() : "yok", th.t, rig.violations);
    if (expect_kritik)
    {
      check(rig.first_kritik_ms != 0, what);
      check(dly <= max_delay_ms, "algılama gecikmesi sınırın içinde");
      check(rig.relays_off(), "KRITIK'te iki röle de kapalı");
    }
    else
    {
      check(rig.first_kritik_ms == 0, what);
      check(rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA, "su kaynadı ve sıcak tutmaya geçildi");
    }
    check(rig.violations == 0, "NORMAL dışındaki hiçbir anda röle açık kalmadı");
    return 0;
  }

  // Az su (2.26 °C/sn) + aynı anda temas sıçraması: gerçek tehlike sıçrama yüzünden kaçmamalı.
  int scenario_az_su_sicrama()
  {
    Rig rig;
    Thermal th{25.0f, 2.26f, 0.02f, true};
    rig.hold(25.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    uint32_t start = millis();
    run_thermal(rig, th, 60000, [](int i, float v) { return i == 1 ? v - 10.5f : v; }, [&] { return rig.first_kritik_ms != 0; });
    uint32_t dly = rig.first_kritik_ms ? rig.first_kritik_ms - start : 0;
    printf("  ölçüm: KRITIK %s · sıcaklık %.1f °C\n", rig.first_kritik_ms ? (std::to_string(dly / 1000.0).substr(0, 4) + " sn sonra").c_str() : "yok", th.t);
    check(rig.first_kritik_ms != 0, "az su, araya tek bozuk okuma girse de yakalanıyor");
    check(dly <= 16000, "en geç bir pencere (7 sn) gecikmeyle");
    check(rig.relays_off() && rig.violations == 0, "röleler kapalı, ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Bozuk okuma desenleri: yanlış alarm vermemeli
  // ---------------------------------------------------------------------------------------------
  // Çay demleyip sıcak tutmaya kadar getirir (MAX: 430 sn demleme + 240 sn dem alma).
  void brew_to_keepwarm(Rig &rig, Thermal &th)
  {
    rig.hold(th.t, 4000);
    rig.cay.publish_state("MAX");
    run_thermal(rig, th, 30 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
  }

  // Sıcak tutmada rölenin açıldığı anı (R) adım hassasiyetiyle yakalar; sonra okumaları R+0.5 sn'den başlayarak
  // 2 sn'de bir yayınlar ve pattern(k, gerçek değer) ile bozar. Böylece hangi okumanın pencere sınırına düştüğü
  // bellidir: su seviye kontrolünün penceresi R'de başlar, R+7 sn'de biter; o anda geçerli okuma k=3'tür (R+6.5 sn),
  // yani k=3 bozuksa bir sonraki pencere bozuk değerle başlar (2 Eki 2026 21:50:18'de olan buydu).
  uint32_t run_pattern_from_relay_on(Rig &rig, Thermal &th, const std::function<float(int, float)> &pattern, uint32_t run_ms)
  {
    uint32_t next_sample = millis();
    auto tick = [&] {
      rig.step();
      th.advance(digitalRead(RELAY) == HIGH, STEP_MS / 1000.0f);
    };
    auto sample_plain = [&] {
      if (millis() >= next_sample)
      {
        rig.feed(th.t);
        next_sample += SAMPLE_MS;
      }
    };
    for (uint32_t el = 0; el < 300000 && digitalRead(RELAY) == HIGH; el += STEP_MS)
    {
      sample_plain();
      tick();
    }
    for (uint32_t el = 0; el < 300000 && digitalRead(RELAY) == LOW; el += STEP_MS)
    {
      sample_plain();
      tick();
    }
    uint32_t r = millis();
    next_sample = r + 500;
    int k = 0;
    for (uint32_t el = 0; el < run_ms; el += STEP_MS)
    {
      if (millis() >= next_sample)
      {
        rig.feed(pattern(k++, th.t));
        next_sample += SAMPLE_MS;
      }
      tick();
    }
    return r;
  }

  int scenario_sicrama(const char *name, const std::function<float(int, float)> &pattern, uint32_t run_ms)
  {
    Rig rig;
    Thermal th{60.0f, 0.17f, 0.05f, true}; // 0.17 °C/sn: o geceki gerçek sıcak tutma ısınma hızı
    brew_to_keepwarm(rig, th);
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA && rig.tazelik.state == "Taze", "hazırlık: çay demlendi, sıcak tutmada ve Taze");
    uint32_t r = run_pattern_from_relay_on(rig, th, pattern, run_ms);
    printf("  ölçüm (%s): KRITIK %s · tazelik %s · mod %s\n", name,
           rig.first_kritik_ms ? ("röle açıldıktan " + std::to_string((rig.first_kritik_ms - r) / 1000.0).substr(0, 4) + " sn sonra VAR").c_str() : "yok",
           rig.tazelik.state.c_str(), rig.mod_durumu.state.c_str());
    check(rig.first_kritik_ms == 0, "bozuk okuma yanlış KRITIK alarmı üretmedi");
    check(rig.tazelik.state == "Taze" && rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA, "çay sıcak tutmada ve Taze kalmaya devam ediyor");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // NaN / sensör kaybı (upstream yazarının PR #4 incelemesinde istediği senaryolar)
  // ---------------------------------------------------------------------------------------------
  int scenario_nan_kaynatirken()
  {
    Rig rig;
    Thermal th{40.0f, 0.30f, 0.02f, true};
    rig.hold(40.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 30000);
    check(digitalRead(RELAY) == HIGH && rig.dev.kettle_durumu_ == NORMAL, "hazırlık: kaynatma sürüyor, röle açık");
    // Sensör 30 sn boyunca NaN veriyor
    long on_before = rig.relay_on_ms;
    for (int i = 0; i < 15; i++)
    {
      rig.feed(NAN);
      rig.run(SAMPLE_MS);
    }
    check(rig.dev.kettle_durumu_ == KORUMA, "NaN gelince KORUMA'ya geçildi");
    check(rig.relays_off(), "iki röle de kapalı");
    check(rig.relay_on_ms - on_before <= (long)STEP_MS, "NaN sürerken röle bir daha açılmadı");
    // Sensör düzeldi
    run_thermal(rig, th, 20000);
    check(rig.dev.kettle_durumu_ == NORMAL, "geçerli okuma gelince NORMAL'e dönüldü");
    check(rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_HAZIRLIK && digitalRead(RELAY) == HIGH, "kaynatma kaldığı yerden sürüyor");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_nan_acilis()
  {
    Rig rig; // sensör henüz hiç okuma yayınlamadı (state = NaN)
    rig.su_kaynatma.publish_state(true); // biri HA'dan hemen kaynatmayı açtı
    rig.run(4000);
    check(rig.dev.kettle_durumu_ == KORUMA, "ilk okuma gelmeden KORUMA");
    check(rig.relays_off() && rig.relay_on_ms == 0, "ilk okuma gelmeden röle hiç açılmadı");
    rig.hold(30.0f, 4000);
    check(rig.dev.kettle_durumu_ == NORMAL, "ilk geçerli okumayla NORMAL");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // Kuru kettle ile KRITIK'e sokar.
  void drive_to_kritik(Rig &rig, bool tea = false)
  {
    Thermal th{25.0f, 6.0f, 0.5f, false};
    rig.hold(25.0f, 4000);
    if (tea)
      rig.cay.publish_state("MAX");
    else
      rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 60000, nullptr, [&] { return rig.first_kritik_ms != 0; });
    rig.hold(th.t, 2000);
  }

  int scenario_kritik_mod_yayini()
  {
    Rig rig;
    drive_to_kritik(rig, true);
    rig.run(1000);
    printf("  ölçüm: kettle=%s aktif_mod=%s mod_durumu=%s seçici=%s tazelik=%s\n", rig.kettle_durumu.state.c_str(), rig.aktif_mod.state.c_str(),
           rig.mod_durumu.state.c_str(), rig.cay.current_option().c_str(), rig.tazelik.state.c_str());
    check(rig.dev.kettle_durumu_ == KRITIK, "kuru kettle KRITIK'e geçirdi");
    check(rig.relays_off(), "iki röle de kapalı");
    check(rig.aktif_mod.state == "KAPALI", "HA'da Aktif Mod KAPALI");
    check(rig.cay.current_option() == "KAPALI", "HA'da demleme seçicisi KAPALI");
    check(rig.mod_durumu.state == "KAPALI", "HA'da Mod Durumu KAPALI");
    check(rig.dev.kritik_sound_active_, "alarm sesi etkin");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_kritik_ha_komutu()
  {
    Rig rig;
    drive_to_kritik(rig);
    check(rig.dev.kettle_durumu_ == KRITIK, "hazırlık: KRITIK");
    // HA'dan sırayla: su kaynat, mama suyu, çay demle MAX
    rig.su_kaynatma.publish_state(true);
    rig.hold(60.0f, 4000);
    bool r1 = rig.relays_off() && !rig.su_kaynatma.state && rig.aktif_mod.state == "KAPALI";
    rig.mama_suyu.publish_state(true);
    rig.hold(60.0f, 4000);
    bool r2 = rig.relays_off() && !rig.mama_suyu.state && rig.aktif_mod.state == "KAPALI";
    rig.cay.publish_state("MAX");
    rig.hold(60.0f, 4000);
    bool r3 = rig.relays_off() && rig.cay.current_option() == "KAPALI" && rig.aktif_mod.state == "KAPALI";
    printf("  ölçüm: su_kaynatma=%d mama=%d seçici=%s aktif_mod=%s mod_durumu=%s\n", (int)rig.su_kaynatma.state, (int)rig.mama_suyu.state,
           rig.cay.current_option().c_str(), rig.aktif_mod.state.c_str(), rig.mod_durumu.state.c_str());
    check(r1, "KRITIK'te HA'dan 'su kaynat' reddedildi; anahtar kapalıya döndü");
    check(r2, "KRITIK'te HA'dan 'mama suyu' reddedildi; anahtar kapalıya döndü");
    check(r3, "KRITIK'te HA'dan 'çay demle MAX' reddedildi; seçici KAPALI'ya döndü");
    check(rig.dev.kettle_durumu_ == KRITIK, "KRITIK sürüyor");
    check(rig.relays_off() && rig.violations == 0, "KRITIK boyunca röle hiç açılmadı");
    return 0;
  }

  int scenario_kritik_kisa_nan()
  {
    Rig rig;
    drive_to_kritik(rig);
    check(rig.dev.kettle_durumu_ == KRITIK, "hazırlık: KRITIK");
    rig.feed(NAN); // tek okumalık sensör kaybı (2 sn)
    rig.run(SAMPLE_MS);
    bool was_koruma = rig.dev.kettle_durumu_ == KORUMA;
    rig.hold(90.0f, 6000);
    printf("  ölçüm: kısa NaN sırasında %s, sonrasında %s, alarm sesi %s\n", was_koruma ? "KORUMA" : "?", rig.kettle_durumu.state.c_str(),
           rig.dev.kritik_sound_active_ ? "etkin" : "kapalı");
    check(was_koruma, "NaN sırasında KORUMA");
    check(rig.dev.kettle_durumu_ == KRITIK, "kısa sensör kaybından sonra KRITIK'e geri dönüldü (koruma atlanmadı)");
    check(rig.dev.kritik_sound_active_, "alarm sesi sürüyor");
    check(rig.relays_off() && rig.violations == 0, "röleler kapalı, ihlal yok");
    return 0;
  }

  int scenario_kritik_kettle_kaldir()
  {
    Rig rig;
    drive_to_kritik(rig);
    check(rig.dev.kettle_durumu_ == KRITIK, "hazırlık: KRITIK");
    int pulses_before = rig.sound_pulses;
    rig.hold(-9.16f, 6000); // kettle 6 sn tabandan kaldırıldı
    bool dark = rig.btn_leds_all_off() && digitalRead(BAY_LED) == LOW && digitalRead(DEM_LED) == LOW;
    int pulses_lifted = rig.sound_pulses - pulses_before;
    rig.hold(80.0f, 10000); // geri kondu
    int pulses_after = rig.sound_pulses - pulses_before - pulses_lifted;
    printf("  ölçüm: kaldırılınca lambalar %s, kaldırılmışken bip %d, geri konunca durum %s, bip %d, aktif_mod %s\n", dark ? "sönük" : "YANIK",
           pulses_lifted, rig.kettle_durumu.state.c_str(), pulses_after, rig.aktif_mod.state.c_str());
    check(pulses_lifted <= 1, "kettle kaldırılınca alarm sesi sustu");
    check(dark, "kettle kaldırılmışken bütün lambalar sönük");
    check(rig.dev.kettle_durumu_ == NORMAL, "kettle kaldırılıp konunca alarm kapandı (NORMAL)");
    check(pulses_after == 0 && !rig.dev.kritik_sound_active_, "geri konduktan sonra alarm sesi yok");
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.aktif_mod.state == "KAPALI", "cihaz boşta: hiçbir mod kendiliğinden devam etmiyor");
    check(rig.relays_off() && rig.violations == 0, "röleler kapalı, ihlal yok");
    // Boşta bekle: kendi kendine ısıtmamalı
    long on = rig.relay_on_ms;
    rig.hold(60.0f, 30000);
    check(rig.relay_on_ms == on, "30 sn boşta: röle açılmadı");
    return 0;
  }

  int scenario_asiri_isinma()
  {
    // "Su kontrolü" anahtarı kapalı olsa bile 120 °C kesmesi çalışmalı, her şeyi kapatmalı ve alarm onaylandıktan
    // (1. tuşa 1,2 sn) sonra ısıtma kendiliğinden yeniden başlamamalı.
    Rig rig(true, false);
    Thermal th{25.0f, 12.0f, 0.5f, false}; // kuru ve çok hızlı: iki okuma arasında 100'ün altından 120'nin üstüne
    rig.hold(25.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 60000, nullptr, [&] { return rig.first_kritik_ms != 0; });
    float t_kritik = th.t;
    rig.hold(th.t, 2000);
    printf("  ölçüm: KRITIK %s (%.1f °C) · aktif_mod %s · su_kaynatma anahtarı %d · alarm sesi %s\n", rig.first_kritik_ms ? "var" : "YOK", t_kritik,
           rig.aktif_mod.state.c_str(), (int)rig.su_kaynatma.state, rig.dev.kritik_sound_active_ ? "etkin" : "kapalı");
    check(rig.first_kritik_ms != 0, "su kontrolü kapalıyken de aşırı ısınma KRITIK'e geçiriyor");
    check(t_kritik < 135.0f, "kesme 120 °C'yi geçen ilk okumada gerçekleşti");
    check(rig.relays_off(), "iki röle de kapalı");
    check(rig.aktif_mod.state == "KAPALI" && !rig.su_kaynatma.state && rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_KAPALI, "mod kapandı, HA'da anahtar kapalı");
    check(rig.dev.kritik_sound_active_, "alarm sesi etkin");
    // Soğudu; kullanıcı 1. tuşa 1,3 sn basarak alarmı onayladı
    rig.hold(60.0f, 4000);
    rig.press(0, 1300);
    rig.hold(60.0f, 2000);
    long on = rig.relay_on_ms;
    rig.hold(60.0f, 30000);
    printf("  ölçüm: uzun basıştan sonra durum %s · aktif_mod %s · sonraki 30 sn'de röle %s\n", rig.kettle_durumu.state.c_str(), rig.aktif_mod.state.c_str(),
           rig.relay_on_ms == on ? "hiç açılmadı" : "AÇILDI");
    check(rig.dev.kettle_durumu_ == NORMAL, "uzun basış KRITIK'ten çıkardı");
    check(rig.relay_on_ms == on && rig.dev.current_mode_ == MODE_KAPALI, "çıkıştan sonra cihaz boşta: ısıtma kendiliğinden yeniden başlamadı");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Fabrika davranışı: kettle kaldırılınca lambalar
  // ---------------------------------------------------------------------------------------------
  int scenario_led_kettle_kaldir()
  {
    Rig rig;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    brew_to_keepwarm(rig, th);
    rig.hold(97.0f, 4000);
    std::string leds_before = rig.btn_leds();
    int dem_before = digitalRead(DEM_LED), bay_before = digitalRead(BAY_LED);
    check(leds_before != "00000" && dem_before == HIGH, "hazırlık: çay sıcak tutmada, mod lambası ve Dem (taze) lambası yanıyor");
    // Kettle 20 sn kaldırıldı: lambaları 100 ms'de bir örnekle, yanıp sönme var mı bak
    int lit_samples = 0;
    for (int i = 0; i < 10; i++)
    {
      rig.feed(-9.16f);
      for (int s = 0; s < 20; s++)
      {
        rig.run(100);
        if (i >= 1 && (!rig.btn_leds_all_off() || digitalRead(DEM_LED) != LOW || digitalRead(BAY_LED) != LOW))
          lit_samples++;
      }
    }
    bool koruma = rig.dev.kettle_durumu_ == KORUMA;
    rig.hold(96.0f, 4000); // geri kondu
    printf("  ölçüm: önce mod lambaları %s Dem=%d Bay=%d · kaldırılmışken yanık görülen örnek %d · sonra %s Dem=%d Bay=%d · tazelik %s\n",
           leds_before.c_str(), dem_before, bay_before, lit_samples, rig.btn_leds().c_str(), digitalRead(DEM_LED), digitalRead(BAY_LED),
           rig.tazelik.state.c_str());
    check(koruma, "kettle kaldırılınca KORUMA");
    check(lit_samples == 0, "kaldırılmışken mod lambası da tazelik lambası da sönük (yanıp sönme yok)");
    check(rig.dev.kettle_durumu_ == NORMAL, "geri konunca NORMAL");
    check(rig.btn_leds() == leds_before, "geri konunca mod lambası eski hâlinde yanıyor");
    check(digitalRead(DEM_LED) == dem_before && digitalRead(BAY_LED) == bay_before, "geri konunca tazelik lambası eski hâlinde");
    check(rig.tazelik.state == "Taze" && rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA, "iş kaldığı yerden sürüyor (Taze, sıcak tutma)");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Diğerleri
  // ---------------------------------------------------------------------------------------------
  int scenario_select_yok()
  {
    // yaml'da cay_demleme tanımlı değil: tuşla çay moduna gir, başka moda geç, kapat (yazarın istediği senaryo).
    Rig rig(false);
    rig.hold(30.0f, 4000);
    rig.press(3); // çay tuşu (1 dokunuş = MAX)
    rig.hold(30.0f, 4000);
    bool tea = rig.dev.current_mode_ == MODE_CAY_DEMLEME;
    rig.press(2); // su kaynatma tuşu → mod değişimi (eskiden null erişimi)
    rig.hold(30.0f, 4000);
    bool boil = rig.dev.current_mode_ == MODE_SU_KAYNATMA;
    rig.press(3);
    rig.hold(30.0f, 4000);
    bool tea2 = rig.dev.current_mode_ == MODE_CAY_DEMLEME;
    rig.press(3); // çay modundayken çay tuşu = iptal
    rig.hold(30.0f, 4000);
    printf("  ölçüm: çay=%d kaynatma=%d çay=%d son mod=%s\n", tea, boil, tea2, rig.aktif_mod.state.c_str());
    check(tea && boil && tea2, "seçici tanımlı değilken tuşlarla mod geçişleri çalışıyor, çökme yok");
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.relays_off(), "iptalden sonra kapalı");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_ota_basliyor()
  {
    // robotea.yaml'daki ota on_begin lambdasının yaptığı çağrılar: güncelleme başlarken her şey kapanmalı.
    Rig rig;
    Thermal th{40.0f, 0.30f, 0.02f, true};
    rig.hold(40.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 20000);
    check(digitalRead(RELAY) == HIGH, "hazırlık: kaynatma sürüyor, röle açık");
    rig.dev.set_mode(MODE_KAPALI, 0);
    rig.dev.reset_all_operations(true);
    bool off_now = rig.relays_off(); // döngü hiç çalışmadan (OTA döngüyü bloklar)
    check(off_now, "OTA başlar başlamaz iki röle de kapalı (döngü beklenmeden)");
    rig.hold(60.0f, 6000); // OTA yarıda kalırsa: cihaz çalışmaya devam eder
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.relays_off() && rig.aktif_mod.state == "KAPALI", "OTA yarıda kalsa da cihaz kapalı kalıyor");
    return 0;
  }

  int scenario_acilis_role()
  {
    // Açılışta LED'ler 3 sn yanıp sönerken röle pinleri çoktan LOW sürülmüş olmalı.
    hoststub::st().pin_level[RELAY] = HIGH; // pin açılışta rastgele yüksekte kalmış olsun
    hoststub::st().pin_level[DEM_RELAY] = HIGH;
    static int relay_at_first_delay = -1, dem_at_first_delay = -1;
    hoststub::st().on_delay = [] {
      if (relay_at_first_delay < 0)
      {
        relay_at_first_delay = digitalRead(RELAY);
        dem_at_first_delay = digitalRead(DEM_RELAY);
      }
    };
    Rig rig;
    hoststub::st().on_delay = nullptr;
    printf("  ölçüm: ilk bekleme (LED yanıp sönmesi) anında ısıtıcı rölesi=%d demleme rölesi=%d\n", relay_at_first_delay, dem_at_first_delay);
    check(relay_at_first_delay == LOW && dem_at_first_delay == LOW, "açılışta röle pinleri LED beklemesinden ÖNCE kapalıya çekiliyor");
    check(rig.relays_off(), "kurulum sonunda röleler kapalı");
    return 0;
  }

  int scenario_replay_1eki()
  {
    // 1 Eki 2026 22:34 → 23:19: soğuk sudan kaynatma, sıcak tutma, kettle kaldırma, kapatıp yeniden kaynatma.
    const int base = 22 * 3600 + 34 * 60 + 30;
    Rig rig;
    int matched = 0, expected = 0;
    std::vector<std::string> misses;
    uint32_t k = replay(rig, "data/2026-10-01-kaynatma.csv", base, 0xFFFFFFFF, matched, expected, misses);
    printf("  ölçüm: KRITIK %s · eşleşen durum geçişi %d/%d · röle toplam açık %.0f sn · ihlal %ld\n", k ? ("@ " + hms(k, base)).c_str() : "yok",
           matched, expected, rig.relay_on_ms / 1000.0, rig.violations);
    for (auto &m : misses)
      printf("    eşleşmeyen: %s\n", m.c_str());
    check(k == 0, "KRITIK yok");
    check(matched == expected, "bütün durum geçişleri yeniden üretildi");
    check(rig.violations == 0, "NORMAL dışındaki hiçbir anda röle açık kalmadı");
    return 0;
  }

  // Temiz (bozuk okuma içermeyen) ısınmada karar eski kodla aynı mı? Tek satır basar; run.sh iki derlemenin
  // çıktısını karşılaştırır. profil: d = doğrusal, h = hızlanan (pencere başında 0.4×, sonunda 1.8× hız), g = ±0.3 °C gürültülü.
  int scenario_tarama(float rate, uint32_t phase_ms, char profile)
  {
    Rig rig;
    uint32_t next_sample = millis() + phase_ms;
    float t = 25.0f;
    uint32_t on_since = 0;
    unsigned noise_state = 12345u + (unsigned)(rate * 100) + phase_ms + (unsigned)profile;
    auto noise = [&]() -> float {
      if (profile != 'g')
        return 0.0f;
      noise_state = noise_state * 1103515245u + 12345u;
      return (((noise_state >> 16) % 601) / 1000.0f) - 0.3f; // -0.3 .. +0.3
    };
    auto tick = [&] {
      if (millis() >= next_sample)
      {
        rig.feed(t + noise());
        next_sample += SAMPLE_MS;
      }
      rig.step();
      bool on = digitalRead(RELAY) == HIGH;
      if (on && on_since == 0)
        on_since = millis();
      if (on)
      {
        float el = (millis() - on_since) / 1000.0f;
        float r = profile == 'h' ? rate * (0.4f + 0.2f * el) : rate;
        if (r > rate * 1.8f)
          r = rate * 1.8f;
        t += r * (STEP_MS / 1000.0f);
      }
    };
    for (uint32_t el = 0; el < 5000; el += STEP_MS)
      tick();
    rig.su_kaynatma.publish_state(true);
    uint32_t start = millis();
    while (millis() - start < 40000 && rig.first_kritik_ms == 0 && t < 93.0f)
      tick();
    if (rig.first_kritik_ms)
      printf("  tarama hız=%.2f faz=%u profil=%c → KRITIK %.2f sn, %.1f °C\n", rate, phase_ms, profile, (rig.first_kritik_ms - start) / 1000.0, t);
    else
      printf("  tarama hız=%.2f faz=%u profil=%c → yok\n", rate, phase_ms, profile);
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_led_diger_modlar()
  {
    // Su kaynatma (hazırlıkta kırmızı) ve mama suyu (hazır olunca beyaz) modlarında da kettle kaldır-koy.
    Rig rig;
    Thermal th{30.0f, 0.30f, 0.02f, true};
    rig.hold(30.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 10000);
    std::string boil = rig.btn_leds();
    rig.hold(-9.16f, 6000);
    bool dark1 = rig.btn_leds_all_off() && digitalRead(DEM_LED) == LOW && digitalRead(BAY_LED) == LOW && rig.relays_off();
    run_thermal(rig, th, 6000);
    bool back1 = rig.btn_leds() == boil && digitalRead(RELAY) == HIGH && rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_HAZIRLIK;
    rig.su_kaynatma.publish_state(false);
    rig.hold(th.t, 4000);
    // mama suyu: 40 °C'ye ısıt, hazır (beyaz) olsun
    th.t = 30.0f;
    rig.hold(30.0f, 4000);
    rig.mama_suyu.publish_state(true);
    run_thermal(rig, th, 120000, nullptr, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    rig.hold(36.0f, 4000);
    std::string mama = rig.btn_leds();
    rig.hold(-9.16f, 6000);
    bool dark2 = rig.btn_leds_all_off() && rig.relays_off();
    rig.hold(36.0f, 4000);
    bool back2 = rig.btn_leds() == mama && rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA;
    printf("  ölçüm: kaynatma lambası %s → kaldırılınca %s → geri %s · mama lambası %s → kaldırılınca %s → geri %s\n", boil.c_str(),
           dark1 ? "sönük" : "YANIK", back1 ? "aynı" : "FARKLI", mama.c_str(), dark2 ? "sönük" : "YANIK", back2 ? "aynı" : "FARKLI");
    check(boil != "00000" && mama != "00000" && boil != mama, "hazırlık: iki modun lambası da yanıyor ve birbirinden farklı");
    check(dark1 && dark2, "kettle kaldırılınca iki modda da lambalar sönük, röleler kapalı");
    check(back1, "su kaynatma: geri konunca lamba aynı, kaynatma sürüyor");
    check(back2, "mama suyu: geri konunca lamba aynı, sıcak tutma sürüyor");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_kaldirilmisken_komut()
  {
    // Kettle tabanda değilken HA'dan su kaynatma açılırsa: ısıtma başlamaz, lambalar sönük kalır;
    // kettle konunca lamba yanar ve kaynatma başlar.
    Rig rig;
    rig.hold(30.0f, 4000);
    rig.hold(-9.16f, 4000);
    rig.su_kaynatma.publish_state(true);
    rig.hold(-9.16f, 6000);
    bool waiting = rig.relays_off() && rig.btn_leds_all_off() && rig.dev.current_mode_ == MODE_SU_KAYNATMA && rig.relay_on_ms == 0;
    Thermal th{30.0f, 0.30f, 0.02f, true};
    run_thermal(rig, th, 8000);
    printf("  ölçüm: kaldırılmışken beklenen durum %s · geri konunca röle %d lamba %s durum %s\n", waiting ? "sağlandı (röle kapalı, lambalar sönük)" : "SAĞLANMADI",
           digitalRead(RELAY), rig.btn_leds().c_str(), rig.mod_durumu.state.c_str());
    check(waiting, "kettle yokken: mod kayıtlı ama röle kapalı, lambalar sönük");
    check(digitalRead(RELAY) == HIGH && !rig.btn_leds_all_off() && rig.dev.kettle_durumu_ == NORMAL, "kettle konunca lamba yandı, kaynatma başladı");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }


  // ---------------------------------------------------------------------------------------------
  // Demleme düzeltmeleri: konuşmanın kesilmemesi, kettle demlerken kaldırılıp konunca su aktarımının sürmesi
  // ---------------------------------------------------------------------------------------------
  // Çay modunu başlatıp DEMLEME_BASLADI'ya kadar getirir; o ânı döndürür (0 = başlamadı).
  uint32_t start_tea_until_brewing(Rig &rig, Thermal &th, uint32_t max_ms = 20 * 60000)
  {
    rig.hold(th.t, 4000); // boşta birkaç saniye: algı girişte işareti görsün
    rig.cay.publish_state("MAX");
    run_thermal(rig, th, max_ms, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_BASLADI; });
    if (rig.dev.cay_demleme_durumu_ != DEMLEME_BASLADI)
      return 0;
    // run_thermal 2 sn'de bir bakar; geçişin gerçek ânı yayınlanan durumdan alınır
    for (auto it = rig.mod_durumu.changes.rbegin(); it != rig.mod_durumu.changes.rend(); ++it)
      if (it->second == "DEMLEME_BASLADI")
        return it->first;
    return millis();
  }

  // Su zaten kaynamışken çay tuşuna basılır: "çayı demlemeye başlıyorum" konuşması başka bir sesle kesilmemeli.
  int konusma_kesilmiyor(Rig &rig, const char *duzen, const char *beklenen_lamba, const char *lamba_aciklamasi)
  {
    rig.hold(100.5f, 4000);
    size_t before = rig.sounds.size();
    rig.press(3); // çay tuşu, tek basış (MAX)
    uint32_t t_press = millis();
    rig.hold(100.5f, 10000);
    uint32_t t_speech = 0;
    int after_speech = 0, before_speech = 0;
    for (size_t i = before; i < rig.sounds.size(); i++)
    {
      if (t_speech == 0 && rig.sounds[i].second == "4")
        t_speech = rig.sounds[i].first;
      else if (t_speech != 0 && rig.sounds[i].first - t_speech < 6000)
        after_speech++;
      else if (t_speech == 0)
        before_speech++;
    }
    std::string seq;
    for (size_t i = before; i < rig.sounds.size(); i++)
      seq += " " + std::to_string((int)(rig.sounds[i].first - t_press)) + "ms:" + rig.sounds[i].second;
    printf("  ölçüm (%s): sesler (tuş bırakıldıktan sonra)%s · konuşmadan sonraki 6 sn'de başka ses %d · lamba %s · durum %s\n", duzen, seq.c_str(),
           after_speech, rig.btn_leds().c_str(), rig.mod_durumu.state.c_str());
    check(t_speech != 0, "demleme başlangıç konuşması çaldı");
    check(after_speech == 0, "konuşmayı izleyen 6 sn içinde başka ses tetiklenmedi (konuşma kesilmiyor)");
    check(before_speech >= 1, "seviye bildirimi bip'i konuşmadan önce çaldı");
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_BASLADI && digitalRead(DEM_RELAY) == HIGH, "demleme başladı, demleme rölesi açık");
    check(rig.btn_leds() == beklenen_lamba, lamba_aciklamasi);
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_cay_sicak_su_konusma_sureli()
  {
    Rig rig;
    return konusma_kesilmiyor(rig, "süreli", "00111", "lamba kırmızı");
  }

  int scenario_cay_kettle_kaldir_sureli()
  {
    // Kettle demleme sırasında kaldırılıp konunca su aktarımı sürmeli (süreli düzen).
    Rig rig;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    run_thermal(rig, th, 60000);
    rig.hold(-9.16f, 20000); // kettle 20 sn tabandan kaldırıldı
    bool off_while_lifted = rig.relays_off() && rig.dev.kettle_durumu_ == KORUMA;
    run_thermal(rig, th, 6000); // geri kondu
    bool resumed = digitalRead(DEM_RELAY) == HIGH;
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    uint32_t total = millis() - t_b;
    printf("  ölçüm: kaldırılmışken röleler %s · geri konunca demleme rölesi %s · röle toplam açık %.1f sn · başlangıçtan hazıra %.1f sn · %s\n",
           off_while_lifted ? "kapalı" : "AÇIK", resumed ? "yeniden açık" : "KAPALI KALDI", rig.dem_relay_on_ms / 1000.0, total / 1000.0, rig.tazelik.state.c_str());
    check(t_b != 0 && off_while_lifted, "kaldırılmışken KORUMA, röleler kapalı");
    check(resumed, "geri konunca demleme rölesi yeniden çekildi");
    check(rig.dem_relay_on_ms >= 400000 && rig.dem_relay_on_ms <= 412000, "su aktarımı kalan süre boyunca sürdü (430 sn − kaldırılan ~22 sn)");
    check(total >= 669000 && total <= 672500 && rig.tazelik.state == "Taze", "çay yine 670 sn'de hazır, Taze");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int usage()
  {
    printf("senaryolar: replay-aksam replay-yeniden az-su yarim-litre kuru az-su-sicrama tek-sicrama ardisik-sicrama toparlanma-adimi\n"
           "            nan-kaynatirken nan-acilis kritik-mod-yayini kritik-ha-komutu kritik-kisa-nan kritik-kettle-kaldir asiri-isinma\n"
           "            led-kettle-kaldir led-diger-modlar kaldirilmisken-komut select-yok ota-basliyor acilis-role replay-1eki\n"
           "            cay-sicak-su-konusma-sureli cay-kettle-kaldir-sureli\n");
    return 2;
  }
} // namespace

int main(int argc, char **argv)
{
  if (argc < 2)
    return usage();
  std::string s = argv[1];
  hoststub::st().verbose = argc > 2 && std::string(argv[2]) == "-v";
  printf("== %s ==\n", s.c_str());

  if (s == "replay-aksam")
    scenario_replay_aksam();
  else if (s == "replay-yeniden")
    scenario_replay_yeniden();
  else if (s == "az-su")
    scenario_su_azligi(2.26f, true, true, 9000, "az su (yazarın ölçümü: 0.1 L ≈ 2.26 °C/sn) KRITIK'e geçiriyor");
  else if (s == "yarim-litre")
    scenario_su_azligi(1.41f, true, false, 0, "yarım litre (≈ 1.41 °C/sn) alarm vermiyor");
  else if (s == "kuru")
    scenario_su_azligi(6.0f, false, true, 9000, "kuru kettle KRITIK'e geçiriyor");
  else if (s == "az-su-sicrama")
    scenario_az_su_sicrama();
  else if (s == "tek-sicrama")
    // 2 Eki 21:50:18'deki gibi: pencere sınırındaki tek okuma (k=3) 10.5 °C düşük
    scenario_sicrama("tek bozuk okuma", [](int k, float v) { return k == 3 ? v - 11.0f : v; }, 40000);
  else if (s == "ardisik-sicrama")
    // 21:50:42–56'daki gibi: bir bozuk, bir düzgün, bir bozuk... (k=3,5,7,9)
    scenario_sicrama("ardışık bozuk okumalar", [](int k, float v) { return (k >= 3 && k <= 9 && k % 2 == 1) ? v - 12.0f : v; }, 40000);
  else if (s == "toparlanma-adimi")
    // temas 6 sn boyunca kötü (k=1..3 hepsi 12 °C düşük), sonra düzeliyor: yukarı doğru tek adım
    scenario_sicrama("kalıcı düşük okuma + toparlanma", [](int k, float v) { return (k >= 1 && k <= 3) ? v - 12.0f : v; }, 40000);
  else if (s == "nan-kaynatirken")
    scenario_nan_kaynatirken();
  else if (s == "nan-acilis")
    scenario_nan_acilis();
  else if (s == "kritik-mod-yayini")
    scenario_kritik_mod_yayini();
  else if (s == "kritik-ha-komutu")
    scenario_kritik_ha_komutu();
  else if (s == "kritik-kisa-nan")
    scenario_kritik_kisa_nan();
  else if (s == "kritik-kettle-kaldir")
    scenario_kritik_kettle_kaldir();
  else if (s == "asiri-isinma")
    scenario_asiri_isinma();
  else if (s == "led-kettle-kaldir")
    scenario_led_kettle_kaldir();
  else if (s == "select-yok")
    scenario_select_yok();
  else if (s == "ota-basliyor")
    scenario_ota_basliyor();
  else if (s == "acilis-role")
    scenario_acilis_role();
  else if (s == "replay-1eki")
    scenario_replay_1eki();
  else if (s == "led-diger-modlar")
    scenario_led_diger_modlar();
  else if (s == "kaldirilmisken-komut")
    scenario_kaldirilmisken_komut();
  else if (s == "cay-sicak-su-konusma-sureli")
    scenario_cay_sicak_su_konusma_sureli();
  else if (s == "cay-kettle-kaldir-sureli")
    scenario_cay_kettle_kaldir_sureli();
  else if (s == "tarama" && argc >= 5)
    scenario_tarama((float)atof(argv[2]), (uint32_t)atoi(argv[3]), argv[4][0]);
  else
    return usage();

  printf("  sonuç: %d beklentiden %d tutmadı\n", g_checked, g_failed);
  return g_failed ? 1 : 0;
}
