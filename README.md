# ☕️ Karaca Çaysever Robotea Pro Connect 4in1 ESPHome Projesi

*Akıllı kettle deneyiminizi bir üst seviyeye taşıyın!*

![Ziyaretçi Sayacı](https://visitor-badge.laobi.icu/badge?page_id=omerfaruk-aran.caysever_robotea)
---

## 📌 Proje Hakkında

Bu proje, **Karaca Çaysever Robotea Pro Connect 4in1** cihazını ESP32 mikrodenetleyicisi ile geliştirmek için hazırlanmıştır. Mevcut cihaz donanımı tersine mühendislik ile analiz edilerek, orijinal işlevselliği korunmuş ve akıllı ev platformlarına entegrasyonu sağlanmıştır. Cihazın işlevselliği optimize edilerek enerji tasarrufu ve kullanıcı deneyimi artırılmıştır.

🔔 **Uyarı:**  
Proje kapsamında cihaz üzerinde yapılan değişiklikler, cihazın garanti kapsamı dışında kalmasına neden olabilir. Lütfen cihazınıza müdahale etmeden önce bunu göz önünde bulundurun.

---

## ✨ Temel Özellikler

### 🌡️ **Anlık Sıcaklık Takibi**
- Suyun sıcaklığını Home Assistant üzerinden izleyin.
- Sıcaklık koruma modları sayesinde enerji tasarrufu yapın.

### ☕ **Gelişmiş Demleme Seviyeleri**
- 4 farklı demleme seviyesi: **1/4, 2/4, 3/4, MAX**.
- Her seviyeye göre belirlenen süre sonunda rezistans kapanır, enerji tasarrufu sağlanır.

### 💧 **Su Bitti Algısı (isteğe bağlı)**
- Fabrika yazılımındaki gibi: demleme rölesi 10 sn'de bir kısa süre bırakılır ve **GPIO34** girişinde şebeke işareti
  kalıp kalmadığına bakılır. İşaret kesildiyse üst haznedeki su bitmiştir; röle bırakılır, su bittikten 15 dk sonra
  "çay demlendi" denir. Pompalama hiçbir durumda seçilen seviyenin süresini aşmaz.
- Üst hazne boşsa (~47 sn'de anlaşılır) demlenme beklenmeden sıcak tutmaya ve tazelik sayacına geçilir.
- Yaml'da `su_bitti_algisi_switch` ile açılır; anahtar kapatılırsa ya da girişte hiç işaret görülmezse demleme eski,
  süreli düzenle yürür. `demleme_hatti_sensor` tanılama sensörü girişteki kenar sayısını gösterir.
- Ayrıntı: [docs/fabrika-yazilimi.md](docs/fabrika-yazilimi.md).

### ⏱️ **Kendiliğinden Kapanma (isteğe bağlı)**
- `otomatik_kapanma: 2h` — mod açıldıktan bu süre sonra cihaz kendini kapatır (fabrika yazılımında 2 saat).

### 🔄 **Kettle Koruma Modu**
- Kettle kaldırıldığında geçici koruma modu.
- Yerine koyulduğunda işlemler kaldığı yerden devam eder.

### 🔊 **Sesli ve Görsel Geri Bildirim**
- **Buton Sesi:** Kullanıcı geri bildirimi için dokunmatik buton sesleri.  
- **Konuşma Sesi:** İşlemlerin durumuna göre sesli uyarılar.  
- **Çay lambası:** tuşa basılınca kırmızı, demleme bitince beyaz. Tek basışta (MAX) tek bip; 2–4 basışta seviye
  beyaz yanıp sönmeyle gösterilir.
- **Ses denemesi (isteğe bağlı):** `id(caysever).ses_dene(maske)` ses çipindeki klipleri doğrudan çalar; örneği
  `example.yaml`'ın sonunda.

### 🌐 **Home Assistant Entegrasyonu**
- ESPHome ile cihazınızı akıllı ev sistemleriyle entegre edin.
- Cihazınızı telefon veya sesli asistanlarla kontrol edin.

### 🛠️ **Yeni Geliştirilen Özellikler**
- **Mod Sensörü:** Aktif modu takip edebilme.
- **Optimize Rezistans Kullanımı:** Gereksiz enerji tüketimi minimuma indirildi.
- **Kritik ve Koruma Modları:** Cihazın güvenliği için özel durum algılama ve LED ile kullanıcı bilgilendirme.
- **Wi-Fi Durum Bildirimi:** Wi-Fi bağlantı durumu LED yanıp sönme geri bildirimiyle sağlanmaktadır.

---

## 🚀 Kurulum ve Kullanım

Kurulum adımları ve teknik detaylar için lütfen Wiki sayfamıza göz atın.  
👉 **[Wiki: Kurulum Rehberi](https://github.com/omerfaruk-aran/caysever_robotea/wiki/Kurulum)**  

### Hızlı Bakış:
1. **ESP32 Firmware Yükleme**  
   - Cihazın içini açarak ESP32 mikrodenetleyicisine bağlantı sağlayın.  
   - Pin bağlantıları için görsel rehberi Wiki'de bulabilirsiniz.

2. **ESPHome/Home Assistant Entegrasyonu**  
   - `example.yaml` dosyasını ESPHome platformuna yükleyerek entegrasyonu tamamlayın.

3. **Yazılım Güncellemeleri**  
   - Güncellemeleri GitHub üzerinden takip edin ve en yeni sürümleri yükleyin.

---

## ⚠️ Dikkat Edilmesi Gerekenler

- **Garanti İhlali:**  
  Proje cihazın içini açmayı gerektirir. Bu, cihazın garanti dışı kalmasına yol açabilir.

- **Elektrik Güvenliği:**  
  Cihaz fişe takılıyken hiçbir müdahalede bulunmayın. Elektrik çarpması riski bulunmaktadır.

- **Boş Çalışma Koruması:**  
  Orijinal yazılımdaki boş çalışmaya bağlı sorunlar, bu firmware ile çözülmüştür. Su bittiğinde rezistans otomatik olarak kapanır.

---

## 🎯 Geliştirme Amacı

Bu proje, cihazın işlevselliğini artırmak ve akıllı ev sistemlerine entegrasyon sağlamak için tasarlandı. Açık kaynak olarak paylaşılan bu yazılım, tersine mühendislik teknikleri kullanılarak geliştirilmiştir ve kullanıcı geri bildirimleri ile sürekli olarak iyileştirilecektir.

---

## 🛠️ Katkıda Bulunun

Projeyi daha iyi hale getirmek için katkıda bulunabilirsiniz:
- **Sorun Bildirme:** [GitHub Issues](https://github.com/omerfaruk-aran/caysever_robotea/issues) sayfasından sorunları bildirin.
- **Kod Katkısı:** Projeyi fork ederek geliştirin ve Pull Request (PR) gönderin.
- **Geri Bildirim:** Yeni özellik önerilerinde bulunun.

---

## 📜 Lisans

Bu proje, MIT lisansı altında açık kaynak olarak sunulmaktadır.  
Daha fazla bilgi için [LICENSE](LICENSE) dosyasına göz atabilirsiniz.

---

## 📞 İletişim

Sorularınız ve önerileriniz için lütfen [GitHub Issues](https://github.com/omerfaruk-aran/caysever_robotea/issues) sayfasını kullanın.  

İyi demlemeler! ☕
