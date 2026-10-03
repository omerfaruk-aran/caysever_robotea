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
  const int BREW_SENSE = 34;             // demleme hattı girişi ("su bitti" algısı)
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
#ifdef CAYSEVER_ROBOTEA_SU_BITTI_ALGISI
    using CayseverRobotea::brew_phase_;
    using CayseverRobotea::brew_pump_ms_;
    using CayseverRobotea::brew_sense_fault_;
    using CayseverRobotea::brew_sense_trusted_;
    using CayseverRobotea::BREW_TIMED;
#endif
  };

  // Demleme donanımının modeli: üst haznedeki su demleme rölesi açıkken azalır; su bitince ısıtıcı kuruda ısınır
  // ve kendi termostatı açar. Demleme rölesi bırakılmışken hat sağlamsa (termostat kapalı) GPIO34'te şebeke kenarları
  // görülür; işaret kettle'ın tabanda olup olmamasına bağlı değildir (cihazda 3 Eki 01:18'de ölçüldü). Kaynak: fabrika yazılımının çözümlemesi; sayılar cihazda 3 Eki 2026 01:0x'te ölçüldü
  // (boşta 120-150 kenar/sn; röle açıkken 0; kuruda ~40 sn'lik çalışmadan sonra işaret kesildi, 7 dk 40 sn sonra döndü).
  struct BrewHw
  {
    bool present = false;       // GPIO34 devresi var mı (false: giriş hep sessiz)
    float water_s = 0.0f;       // üst haznedeki su, "pompalama saniyesi" cinsinden
    bool kettle = true;         // kettle tabanda mı (yalnız senaryoların okunurluğu için; işareti etkilemez)
    bool stuck = false;         // termostat hiç açmıyor (algı hep "su var" der)
    bool thermostat_open = false;
    float dry_s = 0.0f, cool_s = 0.0f;
    float dry_trip_s = 40.0f;   // kuruda bu kadar (röle açık süresi) ısınınca termostat açar (cihazda 36-46 sn arası)
    float reclose_s = 460.0f;   // röle bırakıldıktan bu kadar sonra yeniden kapanır (cihazda 7 dk 40 sn)
    float edges_per_s = 140.0f; // cihazda boşta ölçülen
    double acc = 0.0;
    uint32_t dem_on_ms = 0;     // demleme rölesinin toplam açık kaldığı süre
    uint32_t dem_dry_ms = 0;    // bunun su yokken geçen kısmı
    uint32_t first_off_ms = 0;  // rölenin ilk bırakıldığı an
    uint32_t last_on_edge_ms = 0; // rölenin son çekildiği an
    uint32_t max_gap_ms = 0, gap_start_ms = 0; // pompalama sırasındaki en uzun bırakma
    int on_edges = 0;
    bool was_on = false;

    void advance(bool dem_on, uint32_t ms, bool kettle_ok)
    {
      float dt = ms / 1000.0f;
      if (dem_on && !was_on)
      {
        on_edges++;
        last_on_edge_ms = millis();
        if (gap_start_ms != 0)
          max_gap_ms = std::max(max_gap_ms, millis() - gap_start_ms);
      }
      if (!dem_on && was_on)
      {
        if (first_off_ms == 0)
          first_off_ms = millis();
        gap_start_ms = millis();
      }
      was_on = dem_on;

      (void)kettle_ok;
      if (dem_on)
      {
        dem_on_ms += ms;
        cool_s = 0.0f;
        if (water_s > 0.0f)
          water_s -= dt;
        else
        {
          dem_dry_ms += ms;
          dry_s += dt;
          if (!stuck && dry_s >= dry_trip_s)
            thermostat_open = true;
        }
      }
      else if (thermostat_open)
      {
        cool_s += dt;
        if (cool_s >= reclose_s)
        {
          thermostat_open = false;
          dry_s = 0.0f;
        }
      }

      if (present && !dem_on && !thermostat_open && hoststub::st().isr[BREW_SENSE] != nullptr)
      {
        acc += edges_per_s * dt;
        while (acc >= 1.0)
        {
          hoststub::st().isr[BREW_SENSE]();
          acc -= 1.0;
        }
      }
    }
  };

  struct Rig
  {
    Exposed dev;
    sensor::Sensor ntc, tazelik_kalan;
    switch_::Switch su_kaynatma, mama_suyu, buton_sesi, konusma_sesi, su_kontrol, su_bitti;
    sensor::Sensor demleme_hatti;
    BrewHw hw;
    bool has_brew_sense = false;                             // bu derlemede "su bitti" algısı var ve istenmiş
    std::vector<std::pair<uint32_t, std::string>> sounds;    // ses çipine giden tetikler: (an, pinler)
    std::string last_sound_pat;
    std::vector<uint32_t> sound_ms;                          // her tetiğin pinlerde kaldığı süre (sounds ile aynı sırada)
    std::vector<std::pair<uint32_t, std::string>> led_log;   // tuş lambalarının her değişimi: (an, desen)
    select::Select cay;
    text_sensor::TextSensor aktif_mod, mod_durumu, kettle_durumu, tazelik;

    long steps = 0;
    long violations = 0;         // "NORMAL değilken röle açık" sayısı (her döngü sonunda bakılır)
    uint32_t relay_on_ms = 0;    // ısıtıcı rölesinin toplam açık kaldığı süre
    std::vector<uint32_t> relay_runs; // ısıtıcı rölesinin her kesintisiz açık kalışı (ms)
    int last_relay = LOW;
    uint32_t dem_relay_on_ms = 0; // demleme rölesinin toplam açık kaldığı süre
    int dem_relay_on_count = 0;  // demleme rölesinin kaç kez çekildiği
    int last_dem_relay = LOW;
    uint32_t first_kritik_ms = 0; // ilk KRITIK anı (0 = hiç)
    int sound_pulses = 0;        // ses çipine giden tetik sayısı (GPIO4 yükselen kenar)
    int last_sound_pin = LOW;

    explicit Rig(bool with_select = true, bool su_kontrol_on = true, bool brew_sense = false, uint32_t auto_off_ms = 0)
    {
#ifdef CAYSEVER_ROBOTEA_SU_BITTI_ALGISI
      if (brew_sense)
      {
        dev.set_su_bitti_algisi_switch(&su_bitti);
        dev.set_demleme_hatti_sensor(&demleme_hatti);
        su_bitti.publish_state(true);
        has_brew_sense = true;
      }
      if (auto_off_ms)
        dev.set_otomatik_kapanma(auto_off_ms);
#else
      (void)brew_sense;
      (void)auto_off_ms;
#endif
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
      hw.advance(digitalRead(DEM_RELAY) == HIGH, ms, hw.kettle); // geçen sürede rölenin durumuna göre su ve kenarlar
      dev.host_run_scheduler();
      dev.loop();
      steps++;
      {
        std::string pat;
        for (int sp_pin : SOUND)
          if (digitalRead(sp_pin) == HIGH)
            pat += (pat.empty() ? "" : "+") + std::to_string(sp_pin);
        if (!pat.empty() && pat != last_sound_pat)
        {
          sounds.emplace_back(millis(), pat);
          sound_ms.push_back(0);
        }
        if (!pat.empty())
          sound_ms.back() += ms;
        last_sound_pat = pat;
      }
      {
        std::string l = btn_leds();
        if (led_log.empty() || led_log.back().second != l)
          led_log.emplace_back(millis(), l);
      }
      {
        int rl = digitalRead(RELAY);
        if (rl == HIGH)
        {
          relay_on_ms += ms;
          if (last_relay == LOW)
            relay_runs.push_back(0);
          relay_runs.back() += ms;
        }
        last_relay = rl;
      }
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

  // Gecikmeli ısıl model (mama suyu için). Gerçek cihazda ölçülen davranış: ısıtıcı açıldıktan ~8 sn sonra okuma
  // yükselmeye başlar, vuruş bittikten ~15 sn sonra tepe yapar, suyun kendisinden birkaç derece yukarı taşar ve
  // ~40 sn'de suya oturur. gain = ısıtıcının bir saniyesinin suyu kaç derece ısıttığı (su miktarına bağlı:
  // ~0,65 L için 0,7; 1 L için 0,45; 0,3 L için 1,6).
  struct ThermalLag
  {
    float tw;            // suyun sıcaklığı
    float gain;          // °C / ısıtıcı saniyesi
    float cool = 0.004f; // °C/sn
    float x = 0;         // yoldaki ısı (suya henüz geçmemiş), °C karşılığı
    std::vector<char> hat;
    size_t idx = 0;
    float max_tw = 0;

    float sensor() const { return tw + 2.0f * x; }
    void advance(bool relay_on, float dt_s)
    {
      const size_t n = (size_t)(8.0f / dt_s);
      if (hat.size() != n)
        hat.assign(n, 0);
      const bool u = hat[idx] != 0;
      hat[idx] = relay_on ? 1 : 0;
      idx = (idx + 1) % n;
      if (u)
        x += gain * dt_s;
      const float akis = x / 15.0f * dt_s;
      x -= akis;
      tw += akis - cool * dt_s;
      max_tw = std::max(max_tw, tw);
    }
  };

  void run_lag(Rig &rig, ThermalLag &th, uint32_t ms, const std::function<bool()> &stop = nullptr)
  {
    for (uint32_t el = 0; el < ms; el += SAMPLE_MS)
    {
      rig.feed(th.sensor());
      for (uint32_t st = 0; st < SAMPLE_MS; st += STEP_MS)
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
    // mama suyu: ısıt, hazır (beyaz) olsun
    th.t = 30.0f;
    rig.hold(30.0f, 4000);
    rig.mama_suyu.publish_state(true);
    run_thermal(rig, th, 600000, nullptr, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    rig.hold(42.0f, 4000);
    std::string mama = rig.btn_leds();
    rig.hold(-9.16f, 6000);
    bool dark2 = rig.btn_leds_all_off() && rig.relays_off();
    rig.hold(42.0f, 4000);
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
  const char *const KIRMIZI = "00111", *const BEYAZ = "11000", *const SONUK = "00000";

  // led_log'un [from, to) aralığındaki desenleri "kırmızı beyaz sönük ..." diye yazar
  std::string lamba_sirasi(const Rig &rig, uint32_t from, uint32_t to = 0xFFFFFFFF)
  {
    std::string s;
    for (auto &e : rig.led_log)
      if (e.first >= from && e.first < to)
        s += std::string(s.empty() ? "" : " → ") + (e.second == KIRMIZI ? "kırmızı" : e.second == BEYAZ ? "beyaz" : e.second == SONUK ? "sönük" : e.second.c_str());
    return s;
  }
  int lamba_sayisi(const Rig &rig, const char *desen, uint32_t from, uint32_t to = 0xFFFFFFFF)
  {
    int n = 0;
    for (auto &e : rig.led_log)
      if (e.first >= from && e.first < to && e.second == desen)
        n++;
    return n;
  }


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
    uint32_t t_last_beep = 0;
    for (size_t i = before; i < rig.sounds.size(); i++)
      if (rig.sounds[i].second == "4+32" && rig.sounds[i].first < t_speech)
        t_last_beep = rig.sounds[i].first;
    check(t_speech != 0, "demleme başlangıç konuşması çaldı");
    check(after_speech == 0, "konuşmayı izleyen 6 sn içinde başka ses tetiklenmedi (konuşma kesilmiyor)");
    check(before_speech == 1, "tek basışta tek bip (fabrikadaki gibi), konuşmadan önce");
    check(t_last_beep != 0 && t_speech - t_last_beep >= 600, "bip ile konuşma arasında en az 0,6 sn var");
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

  // ---------------------------------------------------------------------------------------------
  // Demlemede "su bitti" algısı (fabrika düzeni) ve kendiliğinden kapanma
  // ---------------------------------------------------------------------------------------------
  int scenario_cay_sicak_su_konusma()
  {
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 300.0f;
    return konusma_kesilmiyor(rig, "algılı", "00111", "demlerken lamba kırmızı");
  }

  int scenario_cay_su_bitince()
  {
    // Üst haznede 200 sn'lik su var. Fabrika düzeni: 16 sn kesintisiz, sonra 10 sn açık + kısa ölçüm; su bitince
    // röle bırakılır; son açılıştan 900 sn sonra çay hazır.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 200.0f;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    bool trusted = false;
#ifdef CAYSEVER_ROBOTEA_SU_BITTI_ALGISI
    trusted = rig.dev.brew_sense_trusted_;
#endif
    std::string led_brewing;
    bool led_sampled = false;
    run_thermal(rig, th, 40 * 60000, [&](int, float v) {
      if (!led_sampled && rig.dev.cay_demleme_durumu_ == DEMLEME_BASLADI && millis() - t_b > 5000)
      {
        led_brewing = rig.btn_leds();
        led_sampled = true;
      }
      return v;
    }, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    uint32_t t_done = millis();
    uint32_t first_on_stretch = rig.hw.first_off_ms ? rig.hw.first_off_ms - t_b : 0;
    uint32_t steep = t_done - rig.hw.last_on_edge_ms;
    printf("  ölçüm: algı %s · ilk kesintisiz itiş %.1f sn · röle toplam açık %.1f sn (su 200 sn) · kuruda %.1f sn · açma sayısı %d · en uzun ölçüm arası %u ms\n",
           trusted ? "güvenilir" : "YOK", first_on_stretch / 1000.0, rig.hw.dem_on_ms / 1000.0, rig.hw.dem_dry_ms / 1000.0, rig.hw.on_edges, rig.hw.max_gap_ms);
    printf("         son açılıştan çay hazıra %.1f sn · başlangıçtan hazıra %.1f dk · demlerken lamba %s · tazelik %s\n", steep / 1000.0,
           (t_done - t_b) / 60000.0, led_brewing.c_str(), rig.tazelik.state.c_str());
    check(t_b != 0 && trusted, "hazırlık: algı girişte işareti gördü, demleme başladı");
    check(first_on_stretch >= 15900 && first_on_stretch <= 16100, "ilk itiş 16 sn kesintisiz (fabrikadaki gibi)");
    check(rig.hw.dem_on_ms >= 240000 && rig.hw.dem_on_ms <= 251500, "röle su bitene kadar açık kaldı; termostat açınca (kuruda ~40 sn) bırakıldı");
    check(rig.hw.dem_dry_ms <= 51500, "ısıtıcı kuruda yalnız termostatı açana kadar çalıştı (eski düzende 230 sn çalışırdı)");
    check(rig.hw.max_gap_ms <= 300, "pompalama sırasında ölçüm için bırakma 0,3 sn'yi geçmedi");
    check(steep >= 899000 && steep <= 902500, "çay, son röle açılışından 900 sn sonra hazır");
    check(led_brewing == "00111", "demlerken çay lambası kırmızı");
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA && rig.tazelik.state == "Taze" && digitalRead(DEM_LED) == HIGH, "sonunda sıcak tutma, Taze, Dem lambası yanık");
    check(rig.btn_leds() == "11000", "çay hazır olunca çay lambası beyaz");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_cay_bos_hazne()
  {
    // Üst hazne boş (ya da su demleme ısıtıcısına ulaşmıyor), kettle'da su var, çay başlatıldı. Beklenen: cihaz suyu
    // itmeyi dener, ilk dakikada olmadığını anlar; fabrika yazılımındaki gibi hata sayar: her şey kapanır, üç bip,
    // çay lambası üç kez kırmızı yanıp söner, "çay demlendi" denmez, tazelik "Demlenemedi" gösterir.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 0.0f;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    size_t sounds_before = rig.sounds.size();
    run_thermal(rig, th, 15 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ != DEMLEME_BASLADI; });
    uint32_t t_end = millis();
    for (auto it = rig.mod_durumu.changes.rbegin(); it != rig.mod_durumu.changes.rend(); ++it)
      if (it->second == "SICAKLIK_KORUMA" || it->second == "KAPALI")
      {
        t_end = it->first;
        break;
      }
    uint32_t dem_on = rig.hw.dem_on_ms;
    int on_edges = rig.hw.on_edges;
    long relay_before = rig.relay_on_ms;
    // Uyarı sürerken ve sonrasında: su 90 °C'ye soğusa da ısıtıcı açılmamalı
    th.t = 90.0f;
    run_thermal(rig, th, 60000);
    int beeps = 0, done_speech = 0;
    for (size_t i = sounds_before; i < rig.sounds.size(); i++)
    {
      if (rig.sounds[i].second == "4+32")
        beeps++;
      if (rig.sounds[i].second == "4+19")
        done_speech++;
    }
    std::string sira = lamba_sirasi(rig, t_end > 100 ? t_end - 100 : 0);
    int kirmizi = lamba_sayisi(rig, KIRMIZI, t_end > 100 ? t_end - 100 : 0);
    bool no_heat = rig.relay_on_ms == relay_before;
    bool off = rig.dev.current_mode_ == MODE_KAPALI && rig.relays_off() && no_heat;
    std::string taz = rig.tazelik.state;
    int dem_led = digitalRead(DEM_LED);
    bool dark = rig.btn_leds_all_off();
    // Yeni bir mod başlatılınca "Demlenemedi" bilgisi silinir
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 4000);
    std::string taz_sonra = rig.tazelik.state;
    printf("  ölçüm: demleme başladıktan %.1f sn sonra kesildi · demleme rölesi toplam %.1f sn açık, %d kez çekildi · bip %d · \"çay demlendi\" konuşması %d\n",
           t_b ? (t_end - t_b) / 1000.0 : -1.0, dem_on / 1000.0, on_edges, beeps, done_speech);
    printf("         sonra: mod %s · ısıtıcı %s · tazelik %s · Dem lambası %d · çay lambası: %s · yeni mod başlayınca tazelik %s\n", off ? "KAPALI" : "AÇIK",
           no_heat ? "açılmadı" : "AÇILDI", taz.c_str(), dem_led, sira.c_str(), taz_sonra.c_str());
    check(t_b != 0, "hazırlık: demleme başladı");
    check(t_end - t_b >= 46000 && t_end - t_b <= 50500, "su aktarılamadığı ~48 sn'de anlaşıldı (cihazda ölçülen: 48,0 sn)");
    check(on_edges == 4 && dem_on <= 47000, "demleme rölesi dört kez çekildi (ilk itiş + 3 ölçüm arası), termostat açınca bırakıldı");
    check(off, "her şey kapandı: mod KAPALI, iki röle de kapalı; su soğuyunca ısıtıcı açılmadı");
    check(beeps == 3 && done_speech == 0, "üç uyarı bip'i; \"çay demlendi\" denmedi");
    check(kirmizi == 3 && dark && dem_led == LOW, "çay lambası üç kez kırmızı yanıp söndü, sonra bütün lambalar sönük");
    check(taz == "Demlenemedi", "tazelik sensörü \"Demlenemedi\" gösteriyor (Taze değil)");
    check(taz_sonra == "Yok", "yeni bir mod başlatılınca \"Demlenemedi\" silindi");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_cay_az_su()
  {
    // Üst haznede az su var (30 sn'lik): aktarım ilk dakikayı aşıyor, demleme normal biter ("çay demlendi").
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 30.0f;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ != DEMLEME_BASLADI; });
    int done_speech = 0;
    for (auto &snd : rig.sounds)
      if (snd.second == "4+19")
        done_speech++;
    printf("  ölçüm: demleme rölesi toplam %.1f sn açık · başlangıçtan %.1f dk sonra %s · tazelik %s · \"çay demlendi\" %d\n", rig.hw.dem_on_ms / 1000.0,
           (millis() - t_b) / 60000.0, rig.mod_durumu.state.c_str(), rig.tazelik.state.c_str(), done_speech);
    check(t_b != 0 && rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA && rig.tazelik.state == "Taze" && done_speech == 1,
          "az suyla demleme normal bitti: demlenme beklendi, \"çay demlendi\", Taze");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  // 3 Eki 2026 01:02-01:07 gerçek cihaz kaydı (2. paketin ilk hâli yüklüyken, üst hazne boş): sıcaklıklar ve çay
  // başlatma aynen oynatılır. Cihazda DEMLEME_BASLADI 01:05:02.865'te, "su yok" kararı 48,013 sn sonra verildi.
  // Düzenek (donanım modeliyle birlikte) aynı ânı üretiyor mu?
  int scenario_replay_3eki_bos()
  {
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 0.0f;
    auto rows = load_csv("data/2026-10-03-bos-hazne.csv");
    size_t i = 0;
    while (millis() < 300000)
    {
      while (i < rows.size() && rows[i].t_ms <= millis())
      {
        const Row &r = rows[i++];
        if (r.type == "T" || r.type == "t")
          rig.feed(std::stof(r.value));
        else if (r.type == "CMD" && r.value.rfind("cay=", 0) == 0)
          rig.cay.publish_state(r.value.substr(4));
      }
      rig.step();
    }
    uint32_t t_bas = 0, t_son = 0;
    std::string son;
    for (auto &c : rig.mod_durumu.changes)
    {
      if (c.second == "DEMLEME_BASLADI" && t_bas == 0)
        t_bas = c.first;
      else if (t_bas != 0 && t_son == 0 && c.second != "DEMLEME_BASLADI")
      {
        t_son = c.first;
        son = c.second;
      }
    }
    const uint32_t real_bas = 182865, real_son = 230878;
    printf("  ölçüm: DEMLEME_BASLADI düzenekte %.3f sn, cihazda %.3f sn · \"su yok\" kararı düzenekte %.3f sn (%s), cihazda %.3f sn · demleme rölesi %d kez çekildi (cihazda 3-4 \"tik-tak\" duyuldu)\n",
           t_bas / 1000.0, real_bas / 1000.0, t_son / 1000.0, son.c_str(), real_son / 1000.0, rig.hw.on_edges);
    auto near = [](uint32_t a, uint32_t b, uint32_t tol) { return (a > b ? a - b : b - a) <= tol; };
    check(near(t_bas, real_bas, 100), "demleme, gerçek cihazla aynı anda başladı");
    // Cihazdaki sürüm kararı bir sonraki sıcaklık okumasına kadar bekletiyordu (en çok 2 sn); şimdiki kod bekletmiyor.
    check(t_son + 1300 >= real_son && t_son <= real_son + 100, "\"üst haznede su yok\" kararı gerçek cihazla aynı ölçümde verildi (cihazda 48,0 sn sonra)");
    check(rig.hw.on_edges == 4, "röle dört kez çekildi: ilk itiş + üç ölçüm arası");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_cay_algi_yok()
  {
    // Algı istenmiş ama girişte hiç işaret yok (devre farklıysa ya da bozuksa): eski, süreli düzen aynen sürmeli.
    Rig rig(true, true, true);
    rig.hw.present = false;
    rig.hw.water_s = 200.0f;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    std::string led_brewing;
    bool led_sampled = false;
    run_thermal(rig, th, 40 * 60000, [&](int, float v) {
      if (!led_sampled && millis() - t_b > 5000)
      {
        led_brewing = rig.btn_leds();
        led_sampled = true;
      }
      return v;
    }, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    uint32_t total = millis() - t_b;
    printf("  ölçüm: röle toplam açık %.1f sn · açma sayısı %d · başlangıçtan hazıra %.1f sn · demlerken lamba %s\n", rig.hw.dem_on_ms / 1000.0, rig.hw.on_edges,
           total / 1000.0, led_brewing.c_str());
    check(rig.has_brew_sense, "bu sürümde algı seçeneği var");
    check(rig.hw.dem_on_ms >= 429500 && rig.hw.dem_on_ms <= 430500 && rig.hw.on_edges == 1, "işaret yokken eski düzen: röle 430 sn kesintisiz açık");
    check(total >= 669000 && total <= 672500, "çay 430 + 240 sn sonra hazır (eski düzen)");
    check(led_brewing == "00111", "eski düzende demlerken lamba kırmızı");
    check(rig.tazelik.state == "Taze" && rig.violations == 0, "Taze, ihlal yok");
    return 0;
  }

  int scenario_cay_anahtar_kapali()
  {
    // Algı devresi çalışıyor ama kullanıcı Home Assistant'tan "Su Bitti Algısı"nı kapatmış: eski düzen.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 600.0f;
    rig.su_bitti.publish_state(false);
    Thermal th{60.0f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    uint32_t total = millis() - t_b;
    printf("  ölçüm: röle toplam açık %.1f sn · açma sayısı %d · başlangıçtan hazıra %.1f sn\n", rig.hw.dem_on_ms / 1000.0, rig.hw.on_edges, total / 1000.0);
    check(rig.has_brew_sense, "bu sürümde algı seçeneği var");
    check(rig.hw.dem_on_ms >= 429500 && rig.hw.dem_on_ms <= 430500 && rig.hw.on_edges == 1, "anahtar kapalıyken eski düzen: 430 sn kesintisiz");
    check(total >= 669000 && total <= 672500 && rig.tazelik.state == "Taze", "670 sn sonra hazır, Taze");
    return 0;
  }

  int scenario_cay_kettle_kaldir_demlerken()
  {
    // Kettle su aktarımı sırasında (ilk ölçümden hemen önce) kaldırılır: demleme iptal olmamalı, kettle yokken su
    // aktarılmamalı, geri konunca aktarım sürmeli.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 120.0f;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    // İlk itişin sonuna 100 ms kala kaldır
    while (millis() - t_b < 15900)
    {
      if ((millis() / STEP_MS) % (SAMPLE_MS / STEP_MS) == 0)
        rig.feed(100.0f);
      rig.step();
    }
    rig.hw.kettle = false;
    uint32_t t_lift = millis();
    // Sıcaklık sensörü kaldırılmayı 1,5 sn sonra görür; o âna kadar eski değer geçerlidir
    rig.run(1500);
    std::string mode_before_ntc = rig.aktif_mod.state;
    rig.hold(-9.16f, 14000);
    bool koruma = rig.dev.kettle_durumu_ == KORUMA;
    bool off_while_lifted = rig.relays_off();
    rig.hw.kettle = true;
    uint32_t t_put = millis();
    run_thermal(rig, th, 6000);
    bool resumed = digitalRead(DEM_RELAY) == HIGH && rig.dev.cay_demleme_durumu_ == DEMLEME_BASLADI;
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA || rig.dev.current_mode_ == MODE_KAPALI; });
    printf("  ölçüm: kaldırıldıktan 1,5 sn sonra mod %s · kaldırılmışken %s, röleler %s · geri konunca %s · röle toplam açık %.1f sn (su 120 sn) · son: %s / %s\n",
           mode_before_ntc.c_str(), koruma ? "KORUMA" : "?", off_while_lifted ? "kapalı" : "AÇIK", resumed ? "su aktarımı sürüyor" : "SÜRMÜYOR",
           rig.hw.dem_on_ms / 1000.0, rig.aktif_mod.state.c_str(), rig.tazelik.state.c_str());
    (void)t_lift;
    (void)t_put;
    check(mode_before_ntc == "CAY_DEMLEME", "kettle kaldırıldı: demleme iptal edilmedi");
    check(koruma && off_while_lifted, "kaldırılmışken KORUMA, röleler kapalı");
    check(resumed, "geri konunca su aktarımı kaldığı yerden sürdü");
    check(rig.hw.dem_on_ms >= 159000 && rig.hw.dem_on_ms <= 185000, "suyun tamamı aktarıldı; termostat açınca röle bırakıldı");
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA && rig.tazelik.state == "Taze", "çay demlendi (Taze)");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_cay_ust_sinir()
  {
    // Hatta işaret hiç kesilmiyor (termostat açmıyor ya da giriş başka bir işaret görüyor): pompalama seçilen
    // seviyenin süresinde (MAX = 430 sn) durmalı; yani hiçbir durumda eski düzenden uzun sürmez.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.stuck = true;
    rig.hw.water_s = 100.0f;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    run_thermal(rig, th, 60 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA || rig.dev.current_mode_ == MODE_KAPALI; });
    uint32_t steep = millis() - rig.hw.last_on_edge_ms;
    printf("  ölçüm: röle toplam açık %.1f sn · son açılıştan hazıra %.1f sn · son: %s / %s\n", rig.hw.dem_on_ms / 1000.0, steep / 1000.0, rig.aktif_mod.state.c_str(),
           rig.tazelik.state.c_str());
    check(t_b != 0, "hazırlık: demleme başladı");
    check(rig.hw.dem_on_ms >= 430000 && rig.hw.dem_on_ms <= 440500, "işaret hiç kesilmese de pompalama üst sınırda (430 sn, en çok bir döngü fazlası) durdu");
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA && rig.tazelik.state == "Taze", "sonra normal biçimde demlendi (Taze)");
    check(steep >= 899000 && steep <= 902500, "son açılıştan 900 sn sonra");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_algi_firtina()
  {
    // Girişte şebeke işareti olamayacak kadar hızlı kenar var (gürültü): algı kendini kapatmalı, demleme eski düzende yürümeli.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.edges_per_s = 50000.0f;
    rig.hw.water_s = 500.0f;
    Thermal th{100.5f, 0.30f, 0.05f, true};
    uint32_t t_b = start_tea_until_brewing(rig, th);
    bool fault = false, trusted = true;
#ifdef CAYSEVER_ROBOTEA_SU_BITTI_ALGISI
    fault = rig.dev.brew_sense_fault_;
    trusted = rig.dev.brew_sense_trusted_;
#endif
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    printf("  ölçüm: arıza bayrağı %d · güven %d · kesme %s · röle toplam açık %.1f sn · açma sayısı %d\n", fault, trusted,
           hoststub::st().isr[BREW_SENSE] ? "bağlı" : "ayrıldı", rig.hw.dem_on_ms / 1000.0, rig.hw.on_edges);
    check(t_b != 0 && fault && !trusted, "anlamsız hızdaki işaret algıyı devre dışı bıraktı");
    check(hoststub::st().isr[BREW_SENSE] == nullptr, "giriş artık dinlenmiyor");
    check(rig.hw.dem_on_ms >= 429500 && rig.hw.dem_on_ms <= 430500 && rig.hw.on_edges == 1, "demleme eski düzende yürüdü (430 sn)");
    check(rig.tazelik.state == "Taze" && rig.violations == 0, "Taze, ihlal yok");
    return 0;
  }

  int scenario_otomatik_kapanma()
  {
    // Fabrika yazılımı modu açıldıktan 2 saat sonra kapatır. Su kaynatma sıcak tutmada bırakılır.
    const uint32_t two_h = 2 * 60 * 60000;
    Rig rig(true, true, false, two_h);
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(60.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    uint32_t t0 = millis();
    run_thermal(rig, th, two_h - 60000);
    bool on_before = rig.dev.current_mode_ == MODE_SU_KAYNATMA && rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA;
    run_thermal(rig, th, 5 * 60000, nullptr, [&] { return rig.dev.current_mode_ == MODE_KAPALI; });
    uint32_t t_off = millis() - t0;
    long on = rig.relay_on_ms;
    rig.hold(80.0f, 120000);
    printf("  ölçüm: 1 sa 59 dk'da mod %s · kapanma %.1f dk'da · sonrasında röle %s · anahtar %d · lambalar %s\n", on_before ? "açık" : "KAPALI", t_off / 60000.0,
           rig.relay_on_ms == on ? "hiç açılmadı" : "AÇILDI", (int)rig.su_kaynatma.state, rig.btn_leds().c_str());
    check(on_before, "2 saatten önce mod açık ve sıcak tutuyor");
    check(rig.dev.current_mode_ == MODE_KAPALI && t_off >= two_h && t_off <= two_h + 3000, "mod açıldıktan 2 saat sonra cihaz kendini kapattı");
    check(rig.relays_off() && rig.relay_on_ms == on && !rig.su_kaynatma.state && rig.btn_leds_all_off(), "röleler kapalı, Home Assistant'ta anahtar kapalı, lambalar sönük");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_otomatik_kapanma_yok()
  {
    // Seçenek verilmemişse eski davranış: mod süresiz açık kalır.
    Rig rig;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(60.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 3 * 60 * 60000);
    check(rig.dev.current_mode_ == MODE_SU_KAYNATMA, "seçenek yokken 3 saat sonra da mod açık (eski davranış)");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // "Su yok" sabit sıcaklık sınırı: su varken kaynama aşımında yanlış alarm vermemeli, kuru kettle'ı yine kesmeli
  // ---------------------------------------------------------------------------------------------
  int scenario_replay_3eki_kaynatma()
  {
    // 3 Eki 2026 13:29 → 13:33: ~1 L soğuk su kaynatıldı. Okuma 70 sn boyunca 97-98,8 °C'de kaldı (su kaynıyordu),
    // 100 °C'yi görünce başlayan "steam boost" taban okumasını 106,4 °C'ye çıkardı. Cihazdaki kod 13:33:20'de
    // "su yok" diye KRITIK'e geçti; su vardı.
    const int base = 13 * 3600 + 29 * 60;
    Rig rig;
    auto rows = load_csv("data/2026-10-03-kaynatma-106.csv");
    size_t i = 0;
    float max_t = 0;
    uint32_t end = rows.back().t_ms + 1500;
    while (millis() < end)
    {
      while (i < rows.size() && rows[i].t_ms <= millis())
      {
        const Row &r = rows[i++];
        if (r.type == "T" || r.type == "t")
        {
          rig.feed(std::stof(r.value));
          max_t = std::max(max_t, std::stof(r.value));
        }
        else if (r.type == "CMD" && r.value == "su_kaynatma=on")
          rig.su_kaynatma.publish_state(true);
      }
      rig.step();
    }
    uint32_t t_koruma = 0;
    for (auto &c : rig.mod_durumu.changes)
      if (c.second == "SICAKLIK_KORUMA")
        t_koruma = c.first;
    int kaynadi_sesi = 0;
    for (auto &snd : rig.sounds)
      if (snd.second == "32")
        kaynadi_sesi++;
    printf("  ölçüm: en yüksek okuma %.1f °C · KRITIK %s (cihazda 13:33:20) · \"su kaynadı\" %s · konuşma %d kez · ısıtıcı rölesi sonda %s · ihlal %ld\n", max_t,
           rig.first_kritik_ms ? ("@ " + hms(rig.first_kritik_ms, base)).c_str() : "yok", t_koruma ? ("@ " + hms(t_koruma, base)).c_str() : "yok", kaynadi_sesi,
           digitalRead(RELAY) == HIGH ? "açık" : "kapalı", rig.violations);
    check(rig.first_kritik_ms == 0, "su varken kaynama aşımı (106,4 °C) yanlış \"su yok\" alarmı vermedi");
    check(t_koruma != 0 && kaynadi_sesi == 1, "kaynatma tamamlandı: sıcak tutmaya geçildi ve \"su kaynadı\" denildi");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_kuru_sicak_tutmada()
  {
    // Sıcak tutmadayken kettle'da su kalmıyor (ör. boşaltılıp boş geri kondu). 98 °C'nin üstünde eğim kontrolü
    // çalışmaz; kesmeyi sabit sıcaklık sınırı yapar. Kuru ısınma 6 °C/sn, okuma 2 sn'de bir.
    Rig rig;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(60.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 10 * 60000, nullptr, [&] { return rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA; });
    run_thermal(rig, th, 40000); // kaynama sonrası 30 sn'lik bekleme geçsin
    bool keepwarm = rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA && rig.first_kritik_ms == 0;
    // Su artık yok: röle açılınca hızla ısınır, kapalıyken yavaş soğur
    Thermal dry{th.t, 6.0f, 0.5f, false};
    float max_fed = 0;
    float t_at_kritik = 0;
    run_thermal(rig, dry, 5 * 60000, [&](int, float v) {
      max_fed = std::max(max_fed, v);
      return v;
    }, [&] {
      if (rig.first_kritik_ms != 0 && t_at_kritik == 0)
        t_at_kritik = max_fed;
      return rig.first_kritik_ms != 0;
    });
    rig.hold(dry.t, 4000);
    printf("  ölçüm: KRITIK %s · kesildiği andaki okuma %.1f °C · aktif_mod %s · alarm sesi %s\n", rig.first_kritik_ms ? "var" : "YOK", t_at_kritik,
           rig.aktif_mod.state.c_str(), rig.dev.kritik_sound_active_ ? "etkin" : "kapalı");
    check(keepwarm, "hazırlık: su kaynadı, sıcak tutmada, alarm yok");
    check(rig.first_kritik_ms != 0, "kuru kettle sıcak tutmada KRITIK'e geçiriyor");
    check(t_at_kritik < 128.0f, "kesme, sınırı geçen ilk okumada gerçekleşti (115 °C + bir okuma aralığı)");
    check(rig.relays_off() && rig.aktif_mod.state == "KAPALI" && rig.dev.kritik_sound_active_, "röleler ve mod kapalı, alarm sesli");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Çay lambasının sırası (fabrikadaki gibi): basınca kırmızı, kaynatırken ve demlerken kırmızı, "çay hazır"da beyaz
  // ---------------------------------------------------------------------------------------------
  int scenario_cay_lamba_sirasi()
  {
    // Soğuk su, üst haznede 60 sn'lik su; çay tuşuna cihazdan bir kez basılır.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 60.0f;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(th.t, 4000);
    hoststub::st().pin_level[TOUCH[3]] = LOW;
    rig.run(200);
    hoststub::st().pin_level[TOUCH[3]] = HIGH;
    uint32_t t_rel = millis();
    run_thermal(rig, th, 40 * 60000, nullptr, [&] { return rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA; });
    uint32_t t_done = millis();
    for (auto it = rig.mod_durumu.changes.rbegin(); it != rig.mod_durumu.changes.rend(); ++it)
      if (it->second == "SICAKLIK_KORUMA")
      {
        t_done = it->first;
        break;
      }
    uint32_t t_red = 0;
    for (auto &e : rig.led_log)
      if (e.first >= t_rel && e.second == KIRMIZI)
      {
        t_red = e.first;
        break;
      }
    bool brewed = false;
    for (auto &c : rig.mod_durumu.changes)
      if (c.second == "DEMLEME_BASLADI")
        brewed = true;
    printf("  ölçüm: tuş bırakıldıktan %u ms sonra kırmızı · hazır olana kadar sıra: %s · hazır olunca: %s · Dem lambası %d\n", t_red ? t_red - t_rel : 99999,
           lamba_sirasi(rig, t_rel, t_done).c_str(), lamba_sirasi(rig, t_done).c_str(), digitalRead(DEM_LED));
    check(brewed && rig.dev.cay_demleme_durumu_ == DEMLEME_SICAKLIK_KORUMA, "hazırlık: su kaynadı, demlendi, çay hazır");
    check(t_red != 0 && t_red - t_rel <= 100, "tuşa basılır basılmaz lamba kırmızı (mod başlamasını beklemeden)");
    check(lamba_sayisi(rig, BEYAZ, t_rel, t_done) == 0 && lamba_sayisi(rig, SONUK, t_rel, t_done) == 0,
          "kaynatma ve demleme boyunca lamba hep kırmızı: beyaz yanıp sönme yok");
    check(rig.btn_leds() == BEYAZ && digitalRead(DEM_LED) == HIGH, "\"çay hazır\"da lamba beyaza döndü, Dem lambası yandı");
    int bip = 0;
    for (auto &snd : rig.sounds)
      if (snd.second == "4+32" && snd.first >= t_rel - 300)
        bip++;
    printf("         tuşa basıştan çay hazıra kadar bip: %d\n", bip);
    check(bip == 1, "tek basışta tek bip (fabrikadaki gibi)");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_cay_lamba_seviye()
  {
    // Üç basış (2/4): seviye bildirimi korunur (üç beyaz yanıp sönme), sonra kırmızı. HA'dan MAX: yanıp sönme yok.
    Rig rig(true, true, true);
    rig.hw.present = true;
    rig.hw.water_s = 60.0f;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(th.t, 4000);
    uint32_t t0 = millis();
    for (int k = 0; k < 3; k++)
      rig.press(3, 150);
    run_thermal(rig, th, 10000);
    int beyaz = lamba_sayisi(rig, BEYAZ, t0);
    std::string sira = lamba_sirasi(rig, t0);
    std::string secim = rig.cay.current_option();
    bool red_now = rig.btn_leds() == KIRMIZI;
    // modu kapat, HA'dan MAX başlat
    uint32_t t_off = millis();
    rig.cay.publish_state("KAPALI");
    run_thermal(rig, th, 4000);
    uint32_t t1 = millis();
    rig.cay.publish_state("MAX");
    run_thermal(rig, th, 10000);
    auto bip_say = [&](uint32_t from, uint32_t to) {
      int n = 0;
      for (auto &snd : rig.sounds)
        if (snd.second == "4+32" && snd.first >= from && snd.first < to)
          n++;
      return n;
    };
    int bip3 = bip_say(t0, t_off), bip_ha = bip_say(t1, millis());
    printf("  ölçüm: üç basış → seçim %s, sıra: %s, bip %d · HA'dan MAX → sıra: %s, bip %d\n", secim.c_str(), sira.c_str(), bip3,
           lamba_sirasi(rig, t1).c_str(), bip_ha);
    check(secim == "2/4" && beyaz == 3 && red_now, "üç basışta seviye üç beyaz yanıp sönmeyle gösterildi, sonra lamba kırmızı");
    check(bip3 == 4, "üç basışta üç tuş bip'i + bir onay bip'i (seviye bildirimi duruyor)");
    check(lamba_sayisi(rig, BEYAZ, t1) == 0 && rig.btn_leds() == KIRMIZI, "HA'dan MAX: beyaz yanıp sönme yok, lamba kırmızı");
    check(bip_ha == 1, "HA'dan MAX: tek bip");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_cay_ha_sicak_su()
  {
    // Su zaten kaynamışken çay Home Assistant'tan başlatılır: komutun bip'i ile "çayı demlemeye başlıyorum" konuşması
    // üst üste binmemeli (demleme hemen başlayabildiği için aradaki tek şey bırakılan nefes payı).
    Rig rig;
    rig.hold(100.5f, 4000);
    size_t before = rig.sounds.size();
    uint32_t t0 = millis();
    rig.cay.publish_state("MAX");
    rig.hold(100.5f, 10000);
    std::string seq;
    uint32_t t_beep = 0, t_speech = 0;
    bool full = true;
    for (size_t i = before; i < rig.sounds.size(); i++)
    {
      seq += " " + std::to_string((int)(rig.sounds[i].first - t0)) + "ms:" + rig.sounds[i].second;
      if (rig.sounds[i].second == "4+32" && t_beep == 0)
        t_beep = rig.sounds[i].first;
      if (rig.sounds[i].second == "4" && t_speech == 0)
        t_speech = rig.sounds[i].first;
      // Komutun bip'i adımın dışında başlar: ilk adımı sayılmaz, 60 ms'lik tetik 40 ms ölçülür
      seq += "(" + std::to_string(rig.sound_ms[i]) + "ms)";
      if (rig.sound_ms[i] < 40)
        full = false;
    }
    printf("  ölçüm: sesler (komuttan sonra)%s · durum %s · lamba %s\n", seq.c_str(), rig.mod_durumu.state.c_str(), rig.btn_leds().c_str());
    check(rig.sounds.size() - before == 2 && t_beep != 0 && t_speech != 0, "bir bip, bir konuşma; başka ses yok");
    // Bip komut anında (adımın başında) verilir ama adımın sonunda kaydedilir: ölçümde bir adımlık (20 ms) pay var
    check(t_speech + STEP_MS >= t_beep + 600 && full, "konuşma bip'ten en az 0,6 sn sonra, iki tetik de tam süre tutuldu");
    check(rig.dev.cay_demleme_durumu_ == DEMLEME_BASLADI && rig.btn_leds() == KIRMIZI, "demleme başladı, lamba kırmızı");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_cay_fazla_basis()
  {
    // Beş basış dikkate alınmaz: mod başlamaz, ilk basışta yakılan kırmızı lamba söner.
    Rig rig;
    rig.hold(60.0f, 4000);
    for (int k = 0; k < 5; k++)
      rig.press(3, 100);
    rig.hold(60.0f, 6000);
    printf("  ölçüm: aktif_mod %s · lamba %s · röleler %s\n", rig.aktif_mod.state.c_str(), rig.btn_leds().c_str(), rig.relays_off() ? "kapalı" : "AÇIK");
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.relays_off(), "beş basışta mod başlamadı");
    check(rig.btn_leds_all_off(), "lamba sönük kaldı");
    return 0;
  }

  int scenario_kritik_bekleyen_basis()
  {
    // Çay tuşuna basıldı, mod daha başlamadan (1 sn'lik basış sayma süresi içinde) KRITIK'e girildi. Alarm onaylandıktan
    // sonra o eski basış çay modunu kendiliğinden başlatmamalı.
    Rig rig;
    rig.hold(90.0f, 4000);
    rig.su_kaynatma.publish_state(true);
    rig.hold(90.0f, 4000);
    hoststub::st().pin_level[TOUCH[3]] = LOW;
    rig.run(200);
    hoststub::st().pin_level[TOUCH[3]] = HIGH;
    rig.run(40);
    rig.feed(125.0f); // aşırı ısınma okuması: bir sonraki döngüde KRITIK
    rig.run(200);
    bool kritik = rig.dev.kettle_durumu_ == KRITIK;
    rig.hold(125.0f, 4000);
    rig.hold(-9.16f, 6000); // kettle kaldırıldı (onay)
    rig.hold(80.0f, 2000);  // geri kondu
    bool normal = rig.dev.kettle_durumu_ == NORMAL;
    long on = rig.relay_on_ms;
    rig.hold(80.0f, 30000);
    printf("  ölçüm: basıştan sonra %s · onaydan sonra %s · aktif_mod %s · 30 sn'de ısıtıcı %s · lamba %s\n", kritik ? "KRITIK" : "KRITIK DEĞİL",
           rig.kettle_durumu.state.c_str(), rig.aktif_mod.state.c_str(), rig.relay_on_ms == on ? "açılmadı" : "AÇILDI", rig.btn_leds().c_str());
    check(kritik && normal, "hazırlık: basıştan hemen sonra KRITIK, kettle kaldır-koy ile NORMAL");
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.aktif_mod.state == "KAPALI", "alarmdan önceki basış çay modunu başlatmadı");
    check(rig.relay_on_ms == on && rig.relays_off(), "onaydan sonra 30 sn: röle açılmadı");
    check(rig.btn_leds_all_off() && rig.violations == 0, "lambalar sönük, ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Ses çipi tetiği
  // ---------------------------------------------------------------------------------------------
  int scenario_ses_tetik_suresi()
  {
    // Kaynatma biter: "su kaynadı" tetiği (yalnız 3. ses pini) ısıtıcı rölesinin bırakıldığı anda verilir.
    // Tetik fabrikadaki gibi en az 50 ms tutulmalı.
    Rig rig;
    Thermal th{60.0f, 0.30f, 0.05f, true};
    rig.hold(th.t, 4000);
    rig.su_kaynatma.publish_state(true);
    run_thermal(rig, th, 10 * 60000, nullptr, [&] { return rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA; });
    run_thermal(rig, th, 2000);
    int n = 0;
    uint32_t dur = 0;
    uint32_t min_all = 99999, max_all = 0;
    for (size_t i = 0; i < rig.sounds.size(); i++)
    {
      if (rig.sounds[i].second == "32")
      {
        n++;
        dur = rig.sound_ms[i];
      }
      min_all = std::min(min_all, rig.sound_ms[i]);
      max_all = std::max(max_all, rig.sound_ms[i]);
    }
    bool pins_low = digitalRead(4) == LOW && digitalRead(19) == LOW && digitalRead(32) == LOW;
    printf("  ölçüm: \"su kaynadı\" tetiği %d kez, %u ms tutuldu (düzeneğin adımı %u ms) · sonra pinler %s\n", n, dur, STEP_MS, pins_low ? "LOW" : "HIGH KALDI");
    check(rig.dev.su_kaynatma_durumu_ == SU_KAYNATMA_SICAKLIK_KORUMA && n == 1, "kaynatma bitti, \"su kaynadı\" bir kez tetiklendi");
    check(dur >= 50 && dur <= 80, "tetik en az 50 ms tutuldu (fabrika değeri)");
    check(pins_low, "tetikten sonra ses pinleri bırakıldı");
#ifdef CAYSEVER_ROBOTEA_SES_DENEME
    // Tanılama çağrısı: ses anahtarları kapalıyken de her deseni verir
    rig.buton_sesi.publish_state(false);
    rig.konusma_sesi.publish_state(false);
    const char *beklenen[8] = {"", "4", "19", "4+19", "32", "4+32", "19+32", "4+19+32"};
    int dogru = 0;
    for (int mask = 1; mask <= 7; mask++)
    {
      // Tetik döngünün dışında verilir; süre çağrı anından pinlerin bırakıldığı döngüye kadar ölçülür
      uint32_t t_call = millis(), t_low = 0;
      rig.dev.ses_dene((uint8_t)mask);
      std::string seen;
      for (int k = 0; k < 20 && t_low == 0; k++)
      {
        rig.step();
        std::string pat;
        for (int sp_pin : SOUND)
          if (digitalRead(sp_pin) == HIGH)
            pat += (pat.empty() ? "" : "+") + std::to_string(sp_pin);
        if (pat.empty())
          t_low = millis();
        else
          seen = pat;
      }
      if (seen == beklenen[mask] && t_low - t_call >= 50 && t_low - t_call <= 80)
        dogru++;
      else
        printf("         maske %d: desen %s, %u ms\n", mask, seen.c_str(), t_low - t_call);
      rig.hold(99.0f, 2000);
    }
    printf("         ses denemesi: 7 desenden %d'i doğru pinlerle ve ≥ 50 ms verildi (ses anahtarları kapalıyken)\n", dogru);
    check(dogru == 7, "ses denemesi yedi deseni de veriyor");
#endif
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  // ---------------------------------------------------------------------------------------------
  // Mama suyu: fabrika yazılımındaki düzen
  // ---------------------------------------------------------------------------------------------
  const char *const MAMA_KIRMIZI = "10000", *const MAMA_BEYAZ = "01111", *const MAMA_HAZIR_SESI = "4+19+32";

  int ses_sayisi(const Rig &rig, const char *desen, size_t from = 0)
  {
    int n = 0;
    for (size_t i = from; i < rig.sounds.size(); i++)
      if (rig.sounds[i].second == desen)
        n++;
    return n;
  }

  int scenario_mama_sicak_su()
  {
    // Su 100 °C'yken mama suyu istenir (gerçek cihazda iki kez görüldü: eski kod hemen "mama suyu hazır" diyordu).
    // Beklenen: mod başlamaz, üç uyarı bip'i, mama lambası üç kez yanıp söner, Home Assistant anahtarı kapalıya döner.
    Rig rig;
    rig.hold(100.0f, 4000);
    size_t s0 = rig.sounds.size();
    uint32_t t0 = millis();
    rig.mama_suyu.publish_state(true); // Home Assistant'tan
    rig.hold(100.0f, 6000);
    bool ha_red = rig.dev.current_mode_ == MODE_KAPALI && !rig.mama_suyu.state && rig.relays_off();
    int bip_ha = ses_sayisi(rig, "4+32", s0), hazir_ha = ses_sayisi(rig, MAMA_HAZIR_SESI, s0);
    int kirmizi = 0;
    for (auto &e : rig.led_log)
      if (e.first >= t0 && e.second == MAMA_KIRMIZI)
        kirmizi++;
    bool dark = rig.btn_leds_all_off();
    size_t s1 = rig.sounds.size();
    rig.press(0); // cihazdaki tuştan
    rig.hold(100.0f, 6000);
    bool tus_red = rig.dev.current_mode_ == MODE_KAPALI && !rig.mama_suyu.state && rig.relays_off();
    int bip_tus = ses_sayisi(rig, "4+32", s1), hazir_tus = ses_sayisi(rig, MAMA_HAZIR_SESI, s1);
    // 44 °C'de (sınırın altında) başlamalı
    rig.hold(44.0f, 4000);
    rig.mama_suyu.publish_state(true);
    rig.hold(44.0f, 2000);
    bool baslar = rig.dev.current_mode_ == MODE_MAMA_SUYU;
    printf("  ölçüm: HA'dan → mod %s, bip %d, \"mama suyu hazır\" %d, mama lambası %d kez yanıp söndü · tuştan → mod %s, bip %d, \"hazır\" %d · 44 °C'de %s\n",
           ha_red ? "başlamadı" : "BAŞLADI", bip_ha, hazir_ha, kirmizi, tus_red ? "başlamadı" : "BAŞLADI", bip_tus, hazir_tus, baslar ? "başlıyor" : "BAŞLAMIYOR");
    check(ha_red && tus_red, "100 °C'de mama suyu başlamadı (HA'dan ve tuştan); anahtar kapalı, röleler kapalı");
    check(hazir_ha == 0 && hazir_tus == 0, "\"mama suyu hazır\" denmedi");
    check(bip_ha == 4 && bip_tus == 4, "komutun/tuşun bip'i + üç uyarı bip'i");
    check(kirmizi == 3 && dark, "mama lambası üç kez yanıp söndü, sonra sönük");
    check(rig.relay_on_ms == 0 || baslar, "reddedilirken ısıtıcı hiç açılmadı");
    check(baslar, "sınırın altında (44 °C) mod başlıyor");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  // Mama suyu 40 °C: su miktarı (gain) ve başlangıç sıcaklığı ne olursa olsun "hazır" denildiğinde su 40 °C civarında
  // olmalı, hiçbir anda belirgin biçimde aşmamalı. satir=true ise yalnız tek satır yazar (tarama için).
  int scenario_mama_40(float gain, float start, bool satir)
  {
    Rig rig;
#ifdef CAYSEVER_ROBOTEA_MAMA_FABRIKA
    rig.dev.set_mama_suyu_sicak_tutma(60 * 60000);
#endif
    ThermalLag th{start, gain};
    for (int i = 0; i < 20; i++)
      th.advance(false, 0.5f); // model otursun
    th.tw = start;
    rig.hold(start, 4000);
    rig.mama_suyu.publish_state(true);
    uint32_t t0 = millis();
    run_lag(rig, th, 40 * 60000, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    uint32_t t_hazir = millis();
    for (auto &c : rig.mod_durumu.changes)
      if (c.second == "SICAKLIK_KORUMA")
      {
        t_hazir = c.first;
        break;
      }
    bool hazir = rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA;
    float tw_hazir = th.tw, okuma_hazir = th.sensor();
    size_t runs_heat = rig.relay_runs.size();
    uint32_t max_burst = 0;
    for (auto r : rig.relay_runs)
      max_burst = std::max(max_burst, r);
    if (satir)
    {
      run_lag(rig, th, 60000);
      const float tw_1dk = th.tw; // yoldaki ısı suya geçtikten sonra
      run_lag(rig, th, 4 * 60000);
      const bool ok = hazir && tw_1dk >= 38.0f && th.max_tw <= 42.0f;
      printf("mama-tarama kazanç %.2f başlangıç %4.1f → hazır %4.1f dk, %zu vuruş (en uzun %4.1f sn), hazırdan 1 dk sonra su %.1f °C, en yüksek su %.1f °C%s\n", gain,
             start, (t_hazir - t0) / 60000.0, runs_heat, max_burst / 1000.0, tw_1dk, th.max_tw, ok ? "" : "  <-- BANT DIŞI");
      return ok ? 0 : 1;
    }
    int hazir1 = ses_sayisi(rig, MAMA_HAZIR_SESI);
    std::string led_hazir = rig.btn_leds();
    float tw_min = 999;
    // sıcak tutma: 1 saat sonra kapanana kadar
    for (uint32_t el = 0; el < 2 * 60 * 60000 && rig.dev.current_mode_ != MODE_KAPALI; el += SAMPLE_MS)
    {
      rig.feed(th.sensor());
      for (uint32_t st = 0; st < SAMPLE_MS; st += STEP_MS)
      {
        rig.step();
        th.advance(digitalRead(RELAY) == HIGH, STEP_MS / 1000.0f);
      }
      if (rig.dev.current_mode_ == MODE_MAMA_SUYU)
        tw_min = std::min(tw_min, th.tw);
    }
    uint32_t t_kapandi = millis();
    for (auto it = rig.aktif_mod.changes.rbegin(); it != rig.aktif_mod.changes.rend(); ++it)
      if (it->second == "KAPALI")
      {
        t_kapandi = it->first;
        break;
      }
    int hazir2 = ses_sayisi(rig, MAMA_HAZIR_SESI);
    printf("  ölçüm (kazanç %.2f °C/sn, başlangıç %.1f °C): %zu vuruş, en uzunu %.1f sn · hazır: %.1f dk'da, su %.1f °C (okuma %.1f), lamba %s\n", gain, start,
           runs_heat, max_burst / 1000.0, (t_hazir - t0) / 60000.0, tw_hazir, okuma_hazir, led_hazir.c_str());
    printf("         en yüksek su %.1f °C · sıcak tutma: %zu vuruş, en düşük su %.1f °C · \"mama suyu hazır\" %d kez · hazırdan %.1f dk sonra mod %s\n", th.max_tw,
           rig.relay_runs.size() - runs_heat, tw_min, hazir2, (t_kapandi - t_hazir) / 60000.0, rig.aktif_mod.state.c_str());
    check(hazir && hazir1 == 1 && led_hazir == MAMA_BEYAZ, "mama suyu hazır oldu: bir anons, lamba beyaz");
    check(tw_hazir >= 38.0f && tw_hazir <= 41.5f, "\"hazır\" denildiğinde su 38–41,5 °C arasında");
    check(th.max_tw <= 42.5f, "su hiçbir anda 42,5 °C'yi geçmedi");
    check(max_burst <= 12100, "vuruşlar 12 sn'yi geçmiyor");
    check(tw_min >= 37.0f && hazir2 == 1, "sıcak tutma: su 37 °C'nin altına inmedi, yeniden anons yok");
    check(rig.dev.current_mode_ == MODE_KAPALI && rig.relays_off() && t_kapandi - t_hazir >= 3599000 && t_kapandi - t_hazir <= 3603000,
          "hazır olduktan 1 saat sonra mod kendiliğinden kapandı");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_mama_ilik()
  {
    // Su 43 °C: sınırın altında olduğu için mod başlar ama ısıtmaz; okuma 41,5 °C'nin altına inene kadar "hazır" demez.
    Rig rig;
    ThermalLag th{43.0f, 0.7f};
    th.cool = 0.01f;
    rig.hold(43.0f, 4000);
    rig.mama_suyu.publish_state(true);
    run_lag(rig, th, 20000);
    bool bekliyor = rig.dev.current_mode_ == MODE_MAMA_SUYU && rig.dev.mama_suyu_durumu_ == MAMA_SUYU_HAZIRLIK && ses_sayisi(rig, MAMA_HAZIR_SESI) == 0;
    run_lag(rig, th, 10 * 60000, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    printf("  ölçüm: 43 °C'de %s · \"hazır\" su %.1f °C'ye inince · ısıtıcı toplam %.1f sn açık\n", bekliyor ? "bekliyor (anons yok)" : "ANONS VAR", th.tw,
           rig.relay_on_ms / 1000.0);
    check(bekliyor, "su hedefin üstündeyken \"mama suyu hazır\" denmedi");
    check(rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA && th.tw <= 41.5f && th.tw >= 39.0f && rig.relay_on_ms == 0,
          "su 41,5 °C'nin altına inince \"hazır\" dendi; ısıtıcı hiç açılmadı");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int scenario_mama_yeniden()
  {
    // Hazırken üstüne soğuk su eklenir (okuma 34 °C'ye düşer): baştan ısıtılır, hazır olunca yeniden haber verilir.
    Rig rig;
    Thermal th{30.0f, 0.40f, 0.005f, true};
    rig.hold(th.t, 4000);
    rig.mama_suyu.publish_state(true);
    run_thermal(rig, th, 30 * 60000, nullptr, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    bool hazir1 = rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA;
    th.t = 34.0f;
    run_thermal(rig, th, 4000);
    bool geri = rig.dev.mama_suyu_durumu_ == MAMA_SUYU_HAZIRLIK;
    std::string led_isit = rig.btn_leds();
    run_thermal(rig, th, 30 * 60000, nullptr, [&] { return rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA; });
    int hazir = ses_sayisi(rig, MAMA_HAZIR_SESI);
    printf("  ölçüm: ilk hazır %s · 34 °C'de durum %s, lamba %s · sonra %s, lamba %s · \"mama suyu hazır\" %d kez\n", hazir1 ? "var" : "YOK",
           geri ? "HAZIRLIK" : "?", led_isit.c_str(), rig.mod_durumu.state.c_str(), rig.btn_leds().c_str(), hazir);
    check(hazir1 && geri && led_isit == MAMA_KIRMIZI, "su soğuyunca yeniden ısıtmaya geçildi, lamba kırmızı");
    check(rig.dev.mama_suyu_durumu_ == MAMA_SUYU_SICAKLIK_KORUMA && rig.btn_leds() == MAMA_BEYAZ && hazir == 2, "yeniden hazır: lamba beyaz, ikinci anons");
    check(rig.first_kritik_ms == 0 && rig.violations == 0, "KRITIK yok, ihlal yok");
    return 0;
  }

  int scenario_mama_kaldirilmisken_sicak()
  {
    // Mod kettle kaldırılmışken başlatılır, kettle 90 °C suyla geri konur: yine başlamamalı.
    Rig rig;
    rig.hold(30.0f, 4000);
    rig.hold(-9.16f, 4000);
    rig.mama_suyu.publish_state(true);
    rig.hold(-9.16f, 4000);
    bool kuruldu = rig.dev.current_mode_ == MODE_MAMA_SUYU;
    size_t s0 = rig.sounds.size();
    rig.hold(90.0f, 8000);
    int bip = ses_sayisi(rig, "4+32", s0), hazir = ses_sayisi(rig, MAMA_HAZIR_SESI);
    printf("  ölçüm: kaldırılmışken mod %s · 90 °C suyla geri konunca mod %s, anahtar %d, uyarı bip'i %d, \"mama suyu hazır\" %d, ısıtıcı toplam %.1f sn\n",
           kuruldu ? "kuruldu" : "kurulmadı", rig.aktif_mod.state.c_str(), (int)rig.mama_suyu.state, bip, hazir, rig.relay_on_ms / 1000.0);
    check(kuruldu, "hazırlık: kettle yokken mod kuruldu");
    check(rig.dev.current_mode_ == MODE_KAPALI && !rig.mama_suyu.state && rig.relays_off() && rig.relay_on_ms == 0, "sıcak suyla geri konunca mod kapandı, ısıtıcı hiç açılmadı");
    check(bip == 3 && hazir == 0, "üç uyarı bip'i; \"mama suyu hazır\" denmedi");
    check(rig.violations == 0, "ihlal yok");
    return 0;
  }

  int usage()
  {
    printf("senaryolar: replay-aksam replay-yeniden az-su yarim-litre kuru az-su-sicrama tek-sicrama ardisik-sicrama toparlanma-adimi\n"
           "            nan-kaynatirken nan-acilis kritik-mod-yayini kritik-ha-komutu kritik-kisa-nan kritik-kettle-kaldir asiri-isinma\n"
           "            led-kettle-kaldir led-diger-modlar kaldirilmisken-komut select-yok ota-basliyor acilis-role replay-1eki\n"
           "            replay-3eki-bos cay-su-bitince cay-bos-hazne cay-algi-yok cay-anahtar-kapali cay-sicak-su-konusma cay-sicak-su-konusma-sureli\n"
           "            cay-kettle-kaldir-demlerken cay-kettle-kaldir-sureli cay-ust-sinir algi-firtina otomatik-kapanma otomatik-kapanma-yok\n"
           "            replay-3eki-kaynatma kuru-sicak-tutmada cay-lamba-sirasi cay-lamba-seviye cay-fazla-basis\n"
           "            cay-ha-sicak-su kritik-bekleyen-basis ses-tetik-suresi cay-az-su\n"
           "            mama-sicak-su mama-40 mama-ilik mama-yeniden mama-kaldirilmisken-sicak\n");
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
  else if (s == "cay-su-bitince")
    scenario_cay_su_bitince();
  else if (s == "cay-bos-hazne")
    scenario_cay_bos_hazne();
  else if (s == "cay-az-su")
    scenario_cay_az_su();
  else if (s == "mama-sicak-su")
    scenario_mama_sicak_su();
  else if (s == "mama-40")
    return scenario_mama_40(argc > 3 ? (float)atof(argv[2]) : 0.7f, argc > 3 ? (float)atof(argv[3]) : 20.0f, argc > 3) ? 1 : (g_failed ? 1 : (printf("  sonuç: %d beklentiden %d tutmadı\n", g_checked, g_failed), 0));
  else if (s == "mama-ilik")
    scenario_mama_ilik();
  else if (s == "mama-yeniden")
    scenario_mama_yeniden();
  else if (s == "mama-kaldirilmisken-sicak")
    scenario_mama_kaldirilmisken_sicak();
  else if (s == "replay-3eki-bos")
    scenario_replay_3eki_bos();
  else if (s == "cay-algi-yok")
    scenario_cay_algi_yok();
  else if (s == "cay-anahtar-kapali")
    scenario_cay_anahtar_kapali();
  else if (s == "cay-sicak-su-konusma")
    scenario_cay_sicak_su_konusma();
  else if (s == "cay-sicak-su-konusma-sureli")
    scenario_cay_sicak_su_konusma_sureli();
  else if (s == "cay-kettle-kaldir-demlerken")
    scenario_cay_kettle_kaldir_demlerken();
  else if (s == "cay-ust-sinir")
    scenario_cay_ust_sinir();
  else if (s == "cay-kettle-kaldir-sureli")
    scenario_cay_kettle_kaldir_sureli();
  else if (s == "algi-firtina")
    scenario_algi_firtina();
  else if (s == "otomatik-kapanma")
    scenario_otomatik_kapanma();
  else if (s == "otomatik-kapanma-yok")
    scenario_otomatik_kapanma_yok();
  else if (s == "cay-lamba-sirasi")
    scenario_cay_lamba_sirasi();
  else if (s == "cay-lamba-seviye")
    scenario_cay_lamba_seviye();
  else if (s == "cay-fazla-basis")
    scenario_cay_fazla_basis();
  else if (s == "cay-ha-sicak-su")
    scenario_cay_ha_sicak_su();
  else if (s == "kritik-bekleyen-basis")
    scenario_kritik_bekleyen_basis();
  else if (s == "ses-tetik-suresi")
    scenario_ses_tetik_suresi();
  else if (s == "replay-3eki-kaynatma")
    scenario_replay_3eki_kaynatma();
  else if (s == "kuru-sicak-tutmada")
    scenario_kuru_sicak_tutmada();
  else if (s == "tarama" && argc >= 5)
    scenario_tarama((float)atof(argv[2]), (uint32_t)atoi(argv[3]), argv[4][0]);
  else
    return usage();

  printf("  sonuç: %d beklentiden %d tutmadı\n", g_checked, g_failed);
  return g_failed ? 1 : 0;
}
