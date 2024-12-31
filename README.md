# ☕️ Karaca Çaysever Robotea Pro Connect 4in1 ESPHome Projesi

*Akıllı kettle deneyiminizi bir üst seviyeye taşıyın!*

![](https://visitor-badge.laobi.icu/badge?page_id=omerfaruk-aran.caysever_robotea)
---

## 📌 Proje Hakkında

Bu proje, **Karaca Çaysever Robotea Pro Connect 4in1** cihazını ESP32 mikrodenetleyicisi ile geliştirmek için hazırlanmıştır. Yazılım, cihazın mevcut işlevselliğini optimize ederken, Home Assistant ve diğer akıllı ev platformlarına tam entegrasyon sağlar.

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

### 🔄 **Kettle Koruma Modu**
- Kettle kaldırıldığında geçici koruma modu.
- Yerine koyulduğunda işlemler kaldığı yerden devam eder.

### 🔊 **Sesli ve Görsel Geri Bildirim**
- **Buton Sesi:** Kullanıcı geri bildirimi için dokunmatik buton sesleri.  
- **Konuşma Sesi:** İşlemlerin durumuna göre sesli uyarılar.  

### 🌐 **Home Assistant Entegrasyonu**
- ESPHome ile cihazınızı akıllı ev sistemleriyle entegre edin.
- Cihazınızı telefon veya sesli asistanlarla kontrol edin.

### 🛠️ **Gelişmiş Özellikler**
- **Mod Sensörü:** Aktif modu takip edebilme.
- **Optimize Rezistans Kullanımı:** Gereksiz enerji tüketimi minimuma indirildi.

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

Bu proje, cihazın işlevselliğini artırmak ve akıllı ev sistemlerine entegrasyon sağlamak için tasarlandı. Açık kaynak olarak paylaşılan bu yazılım, kullanıcı geri bildirimleri ile sürekli olarak geliştirilecektir.

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
