# Bilgisayarda (host) sınama düzeneği

Bileşenin **gerçek kodu** (`components/caysever_robotea/caysever_robotea.cpp`) değiştirilmeden bilgisayarda derlenir;
ESP32'nin pinleri, saati ve ESPHome'un sensör/anahtar/seçici sınıfları `stubs/` altında taklit edilir. Amaç: ısıtıcı
süren bir cihaza yazılım yüklemeden önce davranışı, özellikle güvenlik yollarını (sensör kaybı, kritik durum, röleler),
enerjili cihaza müdahale etmeden kanıtlamak.

```sh
cd tests/host
./run.sh                    # bütün senaryolar
./run.sh replay-aksam -v    # tek senaryo; -v bileşenin kendi kayıt (ESP_LOGx) satırlarını da basar
SRC=<dizin> ./run.sh        # başka bir kaynakla (ör. eski sürümle karşılaştırma)
./run.sh tarama             # 168 temiz ısınma durumunda karar tablosu (iki sürümün çıktısı diff'lenir)
```

Gereken: bir C++20 derleyicisi (`c++`). Her senaryo ayrı süreçte çalışır, çünkü bileşende fonksiyon içi `static`
değişkenler var. Her döngü adımından sonra şu değişmez denetlenir: **kettle durumu NORMAL değilken iki röle de kapalı.**

## Dosyalar

| Dosya | Ne |
|---|---|
| `stubs/` | Arduino (`millis`, `digitalWrite`, `attachInterrupt`…) ve ESPHome (`Component::set_timeout`, `Sensor`, `Switch`, `Select`, `TextSensor`, `WiFi.onEvent`) taklidi. `Switch::publish_state` gerçekteki gibi aynı değerin tekrarını yutar; `Select` yutmaz |
| `test_main.cpp` | senaryolar; `BrewHw` = demleme donanımının modeli (üst haznedeki su, demleme ısıtıcısının termostatı, GPIO34'teki kenarlar) |
| `data/*.csv` | gerçek bir cihazın Home Assistant geçmişi (sıcaklık okumaları, kullanıcının işlemleri, cihazın yayınladığı durumlar); `tools/fixture_olustur.py` üretir |

## Düzeneğin doğruluğu

2 Eki 2026 akşamının verisi (`data/2026-10-02-aksam.csv`), o gün cihazda çalışan kodla (`main` `3e5ac26` + tazelik
sensörleri) oynatıldığında gerçek cihazın yaptığı **birebir** yeniden üretildi: yanlış KRITIK alarmı aynı saniyede
(21:50:26), aynı eğimle (1.728 °C/sn) ve o akşamın 33 durum geçişinin 33'ü aynı anda.

## Sonuçlar

"Önce" = `main` (`3e5ac26`) + tazelik sensörleri. "Sonra" = bu daldaki kod.

| Senaryo | Önce | Sonra |
|---|---|---|
| `replay-aksam` — 2 Eki 17:45–21:52 gerçek veri | yanlış KRITIK @ 21:50:26 | KRITIK yok; 33/33 geçiş aynı |
| `replay-yeniden` — 2 Eki 21:54–23:30 (Taze → 60 dk → Bayat) | 12/12 | 12/12 |
| `replay-1eki` — 1 Eki 22:34–23:19 (soğuk sudan kaynatma) | 17/17 | 17/17 |
| `az-su` — 2.26 °C/sn (koddaki 0.1 L ölçümü) | KRITIK 7.0 sn | KRITIK 7.0 sn |
| `kuru` — 6 °C/sn | KRITIK 7.0 sn | KRITIK 7.0 sn |
| `yarim-litre` — 1.41 °C/sn | alarm yok | alarm yok |
| `az-su-sicrama` — az su + aynı anda bozuk okuma | KRITIK 7.0 sn | KRITIK 14.0 sn (bir pencere gecikme, 56.6 °C) |
| `tek-sicrama` / `ardisik-sicrama` / `toparlanma-adimi` — bozuk okuma desenleri | yanlış KRITIK | alarm yok, çay Taze kalıyor |
| `nan-kaynatirken`, `nan-acilis` — ölçüm kaybı (#4'te istenen senaryolar) | geçti | geçti |
| `select-yok` — yaml'da `cay_demleme` yok (#4'te istenen senaryo) | geçti | geçti |
| `kritik-mod-yayini` — çay modunda KRITIK | HA'da mod CAY_DEMLEME / MAX kalıyor | hepsi KAPALI |
| `kritik-ha-komutu` — KRITIK'te HA'dan mod başlatma | kabul ediliyor (durum yazısı değişiyor) | reddediliyor, anahtar kapalıya dönüyor |
| `kritik-kisa-nan` — KRITIK'te 2 sn ölçüm kaybı | KRITIK'e dönüyor ama alarm sesi kesiliyor | KRITIK ve alarm sesi sürüyor |
| `kritik-kettle-kaldir` — KRITIK'te kettle 6 sn kaldırılıp konuyor | yine KRITIK | NORMAL, cihaz boşta |
| `asiri-isinma` — su kontrolü kapalı, 120 °C | KRITIK; mod açık kalıyor, sessiz; onaydan sonra **ısıtma kendiliğinden sürüyor** | KRITIK; mod kapalı, alarm sesli; onaydan sonra röle açılmıyor |
| `led-kettle-kaldir`, `led-diger-modlar` — kettle kaldırılınca lambalar | mod lambası yanık, Bay yanıp sönüyor | hepsi sönük; geri konunca eski hâl |
| `kaldirilmisken-komut` — kettle yokken HA'dan başlatma | lamba yanıyor | lamba sönük; konunca yanıp başlıyor |
| `ota-basliyor` — yaml `ota: on_begin` içinden yapılan çağrılar | geçti | geçti |
| `acilis-role` — açılışta röle pinleri | LED beklemesi sırasında sürülmüyor | en başta LOW |
| `cay-sicak-su-konusma-sureli` — su kaynamışken çay tuşu | konuşma 960. ms'de başlıyor, 2060. ms'de seviye bip'i kesiyor | bip 2060. ms, konuşma 2660. ms; sonraki 6 sn'de başka ses yok |
| `cay-kettle-kaldir-sureli` — demlerken kettle 20 sn kaldırılıp konuyor | demleme rölesi bir daha çekilmiyor (toplam 62 sn), 432. sn'de "çay demlendi" | röle yeniden çekiliyor (toplam 410 sn), 670. sn'de hazır |
| **Toplam** | **10 / 25** | **25 / 25** |

### Demlemede "su bitti" algısı ve kendiliğinden kapanma

"Önce" = algı ve kapanma seçenekleri olmayan sürüm (yukarıdaki "sonra"). Bu senaryolar yaml'da
`su_bitti_algisi_switch` ve `otomatik_kapanma` verilmiş gibi kurulur.

**Donanım modelinin doğruluğu:** modelin sayıları gerçek bir cihazda ölçüldü (röle bırakılmışken 140 kenar/sn, üst
hazne boşken termostat kuruda ~40 sn'de açıyor, 7 dk 40 sn sonra kapanıyor; işaret kettle'ın yerinde olmasından
bağımsız). `data/2026-10-03-bos-hazne.csv` (o cihazda algılı sürüm yüklüyken, üst hazne boş, çay başlatılıyor)
oynatıldığında demleme 182,900 sn'de başlıyor (cihazda 182,865) ve röle 4 kez çekiliyor (cihazda 3-4 "tik-tak"
duyuldu). O cihazdaki sürüm "su yok" kararını bir sonraki sıcaklık okumasına kadar bekletiyordu; o sürümle karar
230,920 sn'de (cihazda 230,878), bu sürümle aynı ölçümün sonunda (229,860 sn) veriliyor.

| Senaryo | Önce | Sonra |
|---|---|---|
| `cay-su-bitince` — üst haznede 200 sn'lik su | röle 430 sn açık (230 sn'si kuruda), 11,2 dk'da "demlendi" | 16 sn kesintisiz, sonra 10 sn açık + 0,24 sn ölçüm; röle su bitip termostat açınca bırakılıyor (246 sn); son çekilişten 900 sn sonra "demlendi"; lamba demlerken kırmızı, "demlendi"de beyaz |
| `cay-bos-hazne` — üst hazne boş | demleme rölesi 430 sn açık, sonra "demlendi" | 47 sn'de anlaşılıyor (o sürümde "çay demlendi" + sıcak tutma; sonradan hata sayıldı, aşağıda) |
| `replay-3eki-bos` — gerçek cihaz kaydı | — | yukarıdaki doğruluk ölçümü |
| `cay-sicak-su-konusma` — su kaynamışken çay tuşu (algılı düzen) | — | konuşma kesilmiyor, lamba kırmızı |
| `cay-kettle-kaldir-demlerken` — kettle su aktarımı sırasında kaldırılıyor | — | demleme iptal olmuyor, kettle yokken su aktarılmıyor, geri konunca sürüyor |
| `cay-ust-sinir` — hatta işaret hiç kesilmiyor | — | pompalama seçilen seviyenin süresinde (430 sn + en çok bir döngü) duruyor |
| `cay-algi-yok` — girişte hiç işaret yok | — | süreli düzen birebir (430 + 240 sn, lamba kırmızı) |
| `cay-anahtar-kapali` — "Su Bitti Algısı" anahtarı kapalı | — | süreli düzen birebir |
| `algi-firtina` — girişte 50 000 kenar/sn | — | algı kendini kapatıyor, kesme ayrılıyor, süreli düzen |
| `otomatik-kapanma` — su kaynatma 2 saat açık | kapanmıyor | 120,0. dakikada kapanıyor |
| `otomatik-kapanma-yok` — seçenek verilmemiş | 3 saat sonra da açık | aynı |
| **Toplam (bütün senaryolar)** | **24 / 36** | **36 / 36** |

### "Su yok" sabit sınırı (106 → 115 °C)

| Senaryo | Önce (106 °C) | Sonra (115 °C) |
|---|---|---|
| `replay-3eki-kaynatma` — gerçek cihaz kaydı: ~1 L soğuk su, kaynarken taban okuması 106,4 °C'yi görüyor | "su yok" KRITIK'i 13:33:20'de (cihazla aynı saniye) | alarm yok; 13:33:30'da sıcak tutma ve "su kaynadı" |
| `kuru-sicak-tutmada` — sıcak tutmada kettle kuru (6 °C/sn) | 106,5 °C okumasında kesiyor | 118,5 °C okumasında kesiyor (bir okuma, 2 sn sonra) |
| `az-su`, `kuru` — eğim kontrolü | 7.0 sn | 7.0 sn (değişmedi; tarama 168/168 aynı) |
| **Toplam (bütün senaryolar)** | **37 / 38** | **38 / 38** |

### Ses tetiği ve çay lambasının sırası

"Önce" = yukarıdaki "sonra". Gerçek bir cihazda kaynatma bitiminde "su kaynadı" tetiği verildiği hâlde ses çipi
konuşmadı (tetik 10 ms'lik, ısıtıcı rölesinin bırakıldığı anda); fabrika yazılımı tetiği 50 ms tutuyor. Çay lambası
fabrika yazılımında tuşa basılınca kırmızı yanar, demleme bitince beyaza döner.

| Senaryo | Önce | Sonra |
|---|---|---|
| `ses-tetik-suresi` — kaynatma biter, "su kaynadı" tetiği | 20 ms (10 ms'lik zamanlayıcı, düzeneğin adımı 20 ms) | 60 ms (50 ms'lik zamanlayıcı); `ses_dene()` yedi deseni de veriyor |
| `cay-lamba-sirasi` — çay tuşuna bir kez basılır, çay demlenir | tuş bırakıldıktan 1,66 sn sonra kırmızı; sıra: beyaz → sönük → kırmızı → (kaynayınca) beyaz; iki bip | 20 ms sonra kırmızı; "demlendi"ye kadar hep kırmızı, sonra beyaz; tek bip |
| `cay-lamba-seviye` — üç basış (2/4), sonra Home Assistant'tan MAX | üç beyaz yanıp sönme, sonra kırmızı; MAX'ta da bir beyaz yanıp sönme ve iki bip | üç beyaz yanıp sönme + onay bip'i duruyor; MAX'ta yanıp sönme yok, tek bip |
| `cay-ha-sicak-su` — su kaynamışken Home Assistant'tan MAX | bip, 0,5 sn sonra ikinci bip, 0,6 sn sonra konuşma | bip, 0,6 sn sonra konuşma |
| `cay-fazla-basis` — beş basış | mod başlamıyor | mod başlamıyor, ilk basışta yanan lamba sönüyor |
| `kritik-bekleyen-basis` — çay tuşuna basıldıktan sonraki 1 sn içinde KRITIK | alarm onaylanınca çay modu **kendiliğinden başlıyor**, ısıtıcı açılıyor | basış unutuluyor; onaydan sonra cihaz boşta |
| `cay-su-bitince`, `cay-sicak-su-konusma` — algılı düzende lamba | kaynayınca beyaz | demlerken kırmızı |
| **Toplam (bütün senaryolar)** | **36 / 44** | **44 / 44** |

### İlk dakikada biten demleme: "çay demlendi" yerine hata

"Önce" = yukarıdaki "sonra". Su aktarımı ilk dakikada bitiyorsa çay demlenmemiştir: üst hazne boştur ya da su demleme
ısıtıcısına ulaşmıyordur (cihaz ikisini ayıramaz). Fabrika yazılımı bunu hata sayar.

| Senaryo | Önce | Sonra |
|---|---|---|
| `cay-bos-hazne` — üst hazne boş | 47 sn'de "çay demlendi", sıcak tutma, Taze | 47 sn'de her şey kapanıyor: üç bip, çay lambası üç kez kırmızı, tazelik "Demlenemedi"; su soğuyunca ısıtıcı açılmıyor; yeni mod başlayınca "Demlenemedi" siliniyor |
| `cay-az-su` — üst haznede 30 sn'lik su | — | aktarım ilk dakikayı aşıyor: demlenme bekleniyor, "çay demlendi", Taze |
| **Toplam (bütün senaryolar)** | **44 / 45** | **45 / 45** |

`SRC=<eski sürüm> ./run.sh` ile eski sürümler de derlenebilir: bu sürümdeki seçenekler `CAYSEVER_ROBOTEA_SU_BITTI_ALGISI`
ve `CAYSEVER_ROBOTEA_SES_DENEME` işaretleriyle korunur.

**Tarama** (`./run.sh tarama`): 14 ısınma hızı (1.0–6.0 °C/sn) × 4 örnekleme fazı × 3 profil (doğrusal, hızlanan,
±0.3 °C gürültülü) = 168 temiz ısınma durumu. Karar ve alarm anı iki sürümde **168/168 aynı** (144'ünde alarm). Yani
eğim doğrulaması temiz veride hiçbir alarmı susturmuyor ya da geciktirmiyor.

Taramanın gösterdiği, bu değişiklikle ilgisi olmayan mevcut bir özellik: uç-nokta eğimi okumanın 2 sn'lik örnekleme
fazına göre gerçek hızın ~5/7–9/7 katı çıkabiliyor; 1.5 °C/sn doğrusal ısınma da (eşik 1.65) bazı fazlarda alarm
veriyor. Eşiğe dokunulmadı.

## Düzeneğin kapsamadığı

Donanımın kendisi: rölenin gerçekten bırakması, sensör devresinin elektriksel davranışı, ESP32'nin açılıştaki pin
durumları, ses çipi, Wi-Fi olayları. Bunlar yalnız cihazda görülür.
