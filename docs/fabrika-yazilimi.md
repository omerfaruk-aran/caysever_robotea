# Fabrika yazılımının davranışı

Bu bileşen Karaca'nın yazılımının kopyası değil, dışarıdan gözlemle yeniden yazılmış hâlidir. Bu belge, bir Robotea
Pro Connect 4in1'in kendi flash yedeğindeki **fabrika yazılımının** makine kodundan okunan davranışı özetler: hangi
pin ne iş yapar, çay nasıl demlenir, süreler ve güvenlik kesmeleri. Bileşende "fabrikada nasıldı?" sorusunun
kaynağıdır.

**Kaynak:** cihazın tam flash dökümü (4 MB; etkin bölüm `app0`, ESP-IDF v4.3.6). Döküm bu depoda yok; yalnız okunan
davranış var. Yedekte üreticinin bulut kimlik bilgileri de bulunur; buraya alınmadı.
**Yöntem:** bölümler ayrıldı, ESPHome'un getirdiği `xtensa-esp32-elf-objdump` ile makine kodu çözüldü (§9), uygulama
modülleri elle okundu.
**Güven:** pinler, eşikler ve süreler koddan birebir okundu. Devrenin ne olduğu koddan çıkarımdır; GPIO34'ün yorumu
gerçek cihazda ölçülerek doğrulandı (§1 sonu). GPIO16 / GPIO23 yorumu çıkarım olarak duruyor.

## 1. Pinler

| Pin | Fabrika yazılımı | Bu bileşen |
|---|---|---|
| GPIO35 | NTC (ADC1 kanal 7, 10 bit, 11 dB) | aynı |
| GPIO17 | kettle ısıtıcı rölesi | aynı |
| GPIO18 | demleme rölesi (üst hazneden suyu iten ısıtıcı) | aynı |
| **GPIO16** | **çıkış**: sıfır geçişinde ~0,5 ms'lik tetik darbesi (triyak kapısı — çıkarım). Kettle ısıtıcısını röle çekmeden kısa süreli sürmek için | kullanılmıyor |
| **GPIO23** | **giriş, kesme (her kenar)**: şebeke sıfır geçişi (çıkarım); kesme GPIO16 darbesini başlatır | kullanılmıyor |
| **GPIO34** | **giriş**: demleme rölesi **bırakılmışken** kenar sayılır. Kenar varsa üst haznede su var; yoksa su bitmiş | `su_bitti_algisi_switch` verilirse kullanılır |
| 12 / 14 / 27 / 33 | tuş 1 mama · tuş 2 filtre kahve · tuş 3 su kaynatma · tuş 4 çay (LOW = basılı) | aynı sıra |
| 15, 25, 13, 5, 26 | tuş lambaları (matris) | aynı |
| 22 / 21 | Bay (bayat, kırmızı) / Dem (taze, yeşil) | aynı |
| 4, 19, 32 | ses çipi tetikleri (50 ms darbe) | aynı (10 ms) |

**GPIO34 neyi ölçüyor:** demleme rölesinin açık kontağı üzerinden şebeke işareti. Röle bırakılınca hat, demleme
ısıtıcısı ve onun kendi termostatı üzerinden tamamlanıyorsa girişte kenarlar görünür. Üst haznede su bitince ısıtıcı
kuruda ısınır, termostatı açar, hat kopar, kenarlar kesilir. Fabrika yazılımının ölçmek için röleyi kısa süre
bırakması bu yüzden.

**Gerçek cihazda ölçülen** (bileşenin `demleme_hatti_sensor` tanılama sensörüyle): röle bırakılmışken 120–150
kenar/sn (kettle ısıtıcısı çalışırken 140–200) · demleme rölesi açıkken 0 · üst hazne boşken röle 16 + 10 + 10 sn
çalıştıktan sonraki üç ölçümde işaret var, bir 10 sn daha çalışınca yok (termostat kuruda ~40 sn'de açıyor) · işaret
7 dk 40 sn sonra, kettle'daki su 87 °C'ye inince geri geldi · kettle 10 sn kaldırıldığında işaret **kesilmedi**:
demleme ısıtıcısı ve termostatı gövdede, kettle'ın yerinde olması bu girişi etkilemiyor.

## 2. Zamanlama

- Donanım sayacı 4 kHz kesme üretir (250 µs); saniye sayaçları bunun 4000'e bölünmesiyle yürür. GPIO34 her kesmede
  okunur (yalnız demleme rölesi bırakılmışken), seviye değiştiyse sayaç artar (201'de doyar).
- Ana döngü ~20 ms'de bir döner (FreeRTOS 100 Hz, `vTaskDelay(2)`).
- NTC: ~0,35 sn'de bir sıcaklık (250 ms bekleme + 10 ms arayla 10 örnek; 2.–5. örneklerin ortalaması), **tam sayı °C**.
  10 °C ve üstü düşüş ancak art arda 3 ölçümde görülürse kabul edilir (sıçrama süzgeci). Yükseliş hemen kabul edilir.
- Sıcaklık hesabı: `R = v_mV × 31600 / (3300 − v_mV)`, `T = 1 / (ln(R / 100785) / 3950 + 1/298,15) − 273,15`.
  `example.yaml`'daki değerlerle (bölücü 10 kΩ, R25 = 34 kΩ, B = 3950) aynı gerilimde fabrika yaklaşık 2,2 °C daha
  düşük okur: fabrikanın 85 / 90 / 96 / 115 °C eşikleri bileşenin okumasında 87,1 / 92,1 / 98,2 / 117,5 °C'ye denk gelir
  (hesaptan çıkarım; iki yazılımın gerilim okuması aynı sayıldı).
- Kettle yerinde mi: ham ADC değeri 10–1010 aralığı dışındaysa "kettle yok" → bütün lambalar söner; geri konunca
  lambalar eski hâline döner, kuru çalışma ölçümü yeniden başlar.

## 3. Çay (tuş 4)

| Adım | Ne olur | Süre / koşul |
|---|---|---|
| Tuşa basış | bip, çay lambası **kırmızı**, kettle ısıtıcı açılır. Mod açıkken aynı tuş = kapat (seviye seçimi yok) | — |
| Kaynama | 90 °C'ye kadar röle; sonra GPIO16 ile kısa vuruşlar; "kaynadı" kararı (§5) | su zaten sıcaksa ~4 sn |
| Kaynadı | lamba **beyaz**, konuşma (ses 5: "su kaynadı, çayı demlemeye başlıyorum"), demleme rölesi sürekli açık | 16 sn |
| Su itme | döngü: röle **10 sn açık** → bırak → ~0,15 sn GPIO34 say → kenar ≥ 6 ise yeniden çek | su bitene kadar |
| Su bitti | kenar < 6: röle bırakılmış kalır | — |
| Erken bitti | döngünün ilk **60 sn**'sinde biterse: ısıtıcı ve demleme kapanır, çay lambası kırmızı yanıp söner, ses 3, buluta "hata 2" | üst hazne boş |
| Demlenme | bekleme | son röle çekilişinden **900 sn** (15 dk) sonra |
| Hazır | Dem lambası yanar, konuşma (ses 6) | — |
| Tazelik | Dem yanık; 55. dakikada buluta bildirim; **60 dk** sonra Dem söner, Bay yanar, bip | 3600 sn |
| Kapanış | modun başlangıcından **2 saat** sonra cihaz kendini kapatır (115. dakikada bildirim) | 7200 sn |

Demleme ve bekleme boyunca kettle ısıtıcı "sıcak tut" düzeninde çalışır (§5). Çay lambası: su 85 °C'nin üstünde ve
kaynamışsa beyaz, 85 °C ve altına düşünce kırmızı (yeniden ısıtıyor).

> **Gözlemle çelişiyor (çözülmedi).** Cihazı fabrika yazılımıyla kullanan birinin hatırladığı ve bileşenin süreli
> düzeninde de olan davranış: lamba kaynatırken ve **demlerken kırmızı**, "çay demlendi" denince beyaz. Kod yeniden
> okundu ve yukarıdaki gibi (kaynayınca beyaz; sıcaklık ≤ 85 °C ise kırmızı). Fabrika yazılımlı bir cihazda
> doğrulanana kadar bileşen gözlemi izler: iki düzende de demleme bitene kadar kırmızı.

Filtre kahve (tuş 2, durum 6–10) aynı düzenektir; kod çaydaki durum 13–17'nin birebir eşidir. Farkları: lamba tuş 2'de,
başlangıç konuşması ses 2, hazır anonsu son röle çekilişinden **120 sn** sonra (ses 6, çayla aynı klip), tazelik **40 dk**
(35. dakikada buluta bildirim). Kettle ısıtıcı kahvede de çalışır (alttaki su kaynatılır ve sıcak tutulur); erken bitiş
ve 2 saatte kapanma çaydaki gibidir. Su kaynatma (tuş 3): kaynayınca ses 4; 85 °C'ye düşünce yeniden kaynatır; 2 saatte kapanır. Mama
suyu (tuş 1): su 44 °C'den sıcaksa başlamaz (lamba yanıp söner, "hata 3"); hazır olunca ses 8.


### Mama suyu (tuş 1)

Sıcaklıklar fabrikanın kendi ölçeğinde; parantez içindekiler `example.yaml`'daki değerlerle bileşenin okuması (§2).
Fabrika ısıtıcı işlevine verilen değer, diğer denetimlerde de kullanılan tam sayı sensör okumasıdır; sensör kettle
tabanındadır. Tuşun üstünde 40 yazar; fabrika yazılımıyla suyun 45 okumasında kaç derecede kaldığı ölçülmedi.

| Adım | Fabrika |
|---|---|
| Başlatma | su > 44 °C (45,6) ise başlamaz: mama lambası yanıp söner, "hata 3". Kettle yerinde değilse tuş yok sayılır |
| Isıtma | röle yalnız 25 °C'ye (26,4) kadar; sonra GPIO16 vuruşları, 25 sn'lik çevrimde: ≤ 30 °C → 12 sn, ≤ 35 → 9, < 38 → 8, < 42 → 5, üstü 4 sn |
| Hazır | okuma ≥ 45 °C (46,6): lamba değişir, ses 8 |
| Sıcak tutma | okuma ≤ 38 °C (39,6) olunca vuruşla ısıtır. ≤ 35 °C'ye (36,5) düşerse baştan ısıtır, 45'e çıkınca yeniden ses 8 |
| Kapanış | 2 saat |

**Bileşen bu tabloyu kullanmaz.** Aynı tablo ısıtıcı rölesiyle (GPIO17) uygulanıp gerçek bir cihazda denendi: okuma
46,6 °C'de "hazır" denildiğinde su 44–45 °C'deydi (32 °C'den başlayınca), 42 °C'den başlayınca okuma 52 °C'ye çıktı.
Sensör ısıtıcının 15–20 sn gerisinden geliyor ve vuruştan sonra suyun üstüne taşıp ~40 sn'de oturuyor; röleyle tam
güçte bu tablo suyu hedefin üstüne taşıyor. Fabrikada GPIO16 yolunun aynı gücü verip vermediği bilinmiyor (ölçülemedi).
Bileşen bu yüzden tuşun üstünde yazan 40 °C'yi doğrudan hedefler: kısa vuruş, 40 sn bekleme, ölçüm; "hazır" yalnız
oturmuş okuma 39–41,5 °C arasındayken. Fabrikadan alınanlar: sıcak suyla başlamama ve hazırken yeniden ısıtma.
Kapanış için `mama_suyu_sicak_tutma` ile hazırdan sonraki süre ayrıca sınırlanabilir.

## 4. Demleme denetimi

```
mod 1 (ilk itiş)   : röle sürekli açık
mod 2 (algılamalı) :
  durum 0: sayaç76 = 0, röle bırak                           → durum 2
  durum 1: röle açık; sayaç76 > 9 sn → röle bırak, kenar sayacı = 0 → durum 2
  durum 2: 7 döngü (~0,15 sn) bekle; kenar < 6 ise → durum 3 (BİTTİ)
           (mod 2'nin başından beri ≤ 60 sn geçmişse "erken bitti" bayrağı)
           değilse sayaç76 = 0, röle çek                     → durum 1
  durum 3: röle bırakılmış; sayaç76 saymaya devam eder
çay hazır     : sayaç76 > 900 sn  (kahve: > 120 sn)
```
Fabrikadaki küçük kusur: ilk itişten sonraki ilk ölçüm, itişten önce birikmiş eski sayaçla yapılır; gerçek ilk ölçüm
~26. saniyededir.

## 5. Kettle ısıtıcı denetimi

1. Röle açık, 90 °C'ye kadar. Geçen süre ölçülür (su miktarının göstergesi), 180–280 sn'ye sıkıştırılır.
2. Röle bırakılır, GPIO16 vuruşları başlar: `süre/10 − 14` sn (4–14 sn) kesintisiz.
3. Soğuktan başlandıysa (başlangıç < 90 °C) 60 sn boyunca 5 sn vuruş / 10 sn ara; sonra "kaynadı".
   Sıcaktan başlandıysa 2. adımın sonunda doğrudan "kaynadı".
4. Sıcak tutma: 32 sn'de bir, sıcaklık ≤ 96 °C ise 8 sn vuruş; 96 °C'nin üstünde 76 sn'de bir 5 sn vuruş.
   Sıcaklık ≤ 85 °C'ye düşerse 1. adıma döner (röle).

Yani fabrika "kaynadı"yı 100 °C okumasına değil süreye bağlar ve sıcak tutmada röleyi tıklatmaz.

## 6. Güvenlik kesmeleri

| Koşul | Sonuç |
|---|---|
| sıcaklık > 115 °C | ısıtıcı + demleme kapalı, mod lambası yanıp söner, ses 3, "hata 1" |
| mod başladıktan (ya da kettle geri konduktan) **25 sn** sonra sıcaklık ≥ 25 °C artmışsa ve başlangıç < 65 °C ise | aynı ("su yetersiz"). İlk 3. ve 5. saniyede ≥ 5 °C sıçrama görülürse başlangıç değeri yenilenir. Bu denetim bir kez yapılır, sürekli değil |
| demleme ilk 60 sn'de bitti | "hata 2" (§3) |
| mod 2 saat açık kaldı | kapanır |
| herhangi bir tuşa basış | hata bayraklarını siler |

## 7. Sesler

| No | Pinler (4 / 19 / 32) | Fabrikada kullanıldığı yer | Bileşendeki adı |
|---|---|---|---|
| 1 | 1 / 0 / 1 | tuş bip'i, bayatlama | buton sesi |
| 2 | 0 / 1 / 0 | filtre kahve başlangıcı: "filtre kahveniz hazırlanıyor" | filtre kahve hazırlanıyor |
| 3 | 0 / 1 / 1 | yukarıdaki hata durumları: "hazneye su ekle…" uyarısı | su ekle uyarısı (eskiden "filtre kahve hazır" sanılıyordu) |
| 4 | 0 / 0 / 1 | su kaynadı | su kaynadı |
| 5 | 1 / 0 / 0 | çay: kaynadı, demleme başlıyor | çay demleme başlangıcı |
| 6 | 1 / 1 / 0 | çay / kahve hazır: "içeceğiniz hazır, afiyet olsun" (iki içecek için ortak) | çay demlendi |
| 7 | 1 / 0 / 1 (uzun) | sessize alma onayı | — |
| 8 | 1 / 1 / 1 | mama suyu hazır | mama suyu hazır |

2, 3 ve 6 numaralı kliplerin ne dediği gerçek cihazda dinlendi (`ses_dene` ile).

Sessiz modda konuşmalar çalmaz, yerine bip çalar. Sessiz modu: tuş 4 + tuş 1'e birlikte ~2 sn basış.

## 8. Bileşenle karşılaştırma

| Konu | Fabrika | Bileşen |
|---|---|---|
| Demlemenin bitişi | GPIO34 ile "su bitti" | `su_bitti_algisi_switch` varsa aynı döngü; yoksa sabit süre: 430 / 330 / 240 / 150 sn (tuşa 1–4 basış) |
| Su bittikten sonra bekleme | 900 sn | algıyla 900 sn; süreli düzende 240 sn |
| Üst hazne boşken (ilk 60 sn'de bitti) | ses 3, her şey kapanır, çay lambası kırmızı yanıp söner | aynı mantık: ~47 sn'de anlaşılır, her şey kapanır, üç bip, çay lambası üç kez yanıp söner, tazelik "Demlenemedi" |
| Kettle'da su yok (§6: > 115 °C ya da hızlı ısınma) | ısıtıcı + demleme kapalı, mod lambası yanıp söner, ses 3 | KRITIK: röleler hemen kapanır, lambalar yanıp söner, alarm. Konuşma sesi açıksa önce ses 3 ("su ekleyin", 4-5 sn) bir kez çalar, alarm 7 sn sonra başlar; kapalıysa yalnız alarm. 120 °C kesmesinde klip çalmaz |
| Çay tuşu | aç / kapat; seviye yok | kapalıyken 1–4 basış = seviye, açıkken basış = kapat |
| Çay lambası | kodda: ısıtırken kırmızı, kaynayınca beyaz (gözlem farklı, §3'teki not) | tuşa basılınca kırmızı, demleme bitene kadar kırmızı, "çay demlendi"de beyaz |
| Kendiliğinden kapanma | 2 saat | `otomatik_kapanma` ile (ör. `2h`) |
| Kettle kaldırılınca lambalar | söner | söner |
| Su yetersiz algısı | 25 sn'de 25 °C (bir kez) + 115 °C | 7 sn'lik pencerede 1,65 °C/sn (sürekli, sıçrama doğrulamalı) + 106 °C + 120 °C |
| "Kaynadı" kararı | süre + vuruş düzeni | ≥ 100 °C okuması |
| Sıcak tutma | GPIO16 vuruşları (röle tıklamaz) | röle, en az 5 sn arayla |
| Tazelik | çay 60 dk, kahve 40 dk | çay 60 dk |

## 9. Yeniden üretmek için

Adresler `app0` içindir: makine döngüsü `0x400dba3c` (durum tablosu `0x3f4079ec`), tuş işleyici `0x400db708`,
hata denetimi `0x400db5c0`, ısıtıcı `0x400deb48`, demleme `0x400df004`, GPIO34 sayacı `0x400deaf8`, NTC
`0x400dd600–0x400dd814`, ses `0x400dd838`, sayaç kesmesi `0x40083508`, GPIO23 kesmesi `0x400834f0`.
Çözücü (ESPHome'un indirdiği araç zincirinde): `xtensa-esp32-elf-objdump -D -b binary -m xtensa
--adjust-vma=<bölüm adresi> <bölüm.bin>`. Sürücü işlevleri `__FUNCTION__` yazılarından (`gpio_set_level`,
`adc1_get_raw`), pinler bu işlevlere yapılan çağrılardaki sabitlerden bulunur; `switch` tabloları `.rodata`'da kod
adresi olarak durur.
