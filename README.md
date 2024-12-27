# ☕️ Karaca Çaysever Robotea Pro Kettle/Çaycı Firmware Projesi (ESP32 Tabanlı)

Bu proje, **Karaca Çaysever Robotea Pro Connect 4in1** cihazı için geliştirilmiştir. Firmware, ESP32 mikrodenetleyicisi kullanılarak cihazın mevcut özelliklerini iyileştirmek ve yeni işlevler kazandırmak amacıyla tasarlanmıştır.

📢 **Uyarı:** Bu yazılım şu anda yalnızca **Karaca Çaysever Robotea Pro Connect 4in1** modelinde test edilmiştir. Diğer modellerde farklılık gösterebilir. Cihaz üzerinde yapılan değişiklikler cihazınızın garanti kapsamından çıkmasına neden olabilir.

---

## 📌 Önemli Hususlar

⚠️ **Garanti İhlali:**  
Bu firmware, cihazın içini açarak ESP32'ye yazılım yüklemeyi gerektirir. Bu işlem cihazın garanti kapsamı dışında kalmasına neden olur.

⚠️ **Sorumluluk Reddi:**  
Bu proje tamamen eğitsel ve deneysel amaçlar için hazırlanmıştır. Yapılan değişikliklerden kaynaklanan herhangi bir sorundan veya zarardan **sorumlu değilim**.

⚡ **Elektrik Güvenliği:**  
Cihaz fişe takılıyken hiçbir işlem yapmayınız. Elektrik çarpması riski bulunmaktadır.

🛠️ **Geliştirme Süreci:**  
Proje, tersine mühendislik ile cihazın çalışma prensipleri çözülerek oluşturulmuştur.  
Şu an **filtre kahve işlevi** aktif değildir; ilerleyen zamanlarda geliştirilecektir.

---

## ✨ Proje Özellikleri

### 🌟 Enerji Tasarrufu
- Demleme ve sıcaklık koruma süreçlerinde optimize edilmiş rezistans kontrolü sayesinde gereksiz enerji tüketimi önlenmiştir.

### ☕ Demleme Seviyesi İşlevi
- 4 farklı seviye: **1/4, 2/4, 3/4 ve MAX** arasında seçim yapabilme.
- Seviye seçimi ile rezistans yalnızca ihtiyaç kadar çalışır ve su bittiğinde otomatik kapanır.  
  ✔️ Cihazın korunması ve enerji tasarrufu sağlanır.  
  ✔️ Görsel ve sesli bildirimlerle kullanıcı bilgilendirilir.

### 🔄 Kettle Koruma Modu
- Cihaz çalışırken kettle kaldırıldığında geçici olarak koruma moduna geçer. Yerine koyulduğunda işlem devam eder.

### 🌐 ESPHome ve Home Assistant Entegrasyonu
- **Akıllı ev sistemlerine entegrasyon.**  
- Telefon uygulamaları ve sesli asistanlarla kontrol imkanı.

### 🌡️ Anlık Sıcaklık Takibi
- Cihazdaki suyun sıcaklığını Home Assistant üzerinden anlık olarak izleyebilirsiniz.

### 🔥 Sıcaklık Koruma Modu
- Gereksiz rezistans çalışmasını engelleyerek suyun sıcaklığını korur.

### 🔍 Aktif Mod Sensörü
- Cihazın hangi modda çalıştığını takip edebilmek için sensör desteği eklenmiştir.

---

## 🔧 Kurulum Rehberi

📖 **Kurulum için rehberimize göz atın:**  
👉 [Wiki: Kurulum Rehberi](https://github.com/omerfaruk-aran/caysever_robotea/wiki/Kurulum)

1. **ESP32 Firmware Yükleme:**  
   - Cihazın içini açarak ESP32 mikrodenetleyicisine bağlantı sağlanmalıdır.  
   - Bağlantı noktalarını gösteren resimlerle rehber Wiki sayfasında sunulmuştur.  

2. **ESPHome/Home Assistant Entegrasyonu:**  
   - ESPHome dosyasını yükleyerek cihazınızı Home Assistant ile kontrol edebilirsiniz.

3. **Yazılım Güncellemeleri:**  
   - Projeyi GitHub üzerinden takip ederek en son güncellemeleri yükleyebilirsiniz.

---

## ⚠️ Dikkat Edilmesi Gerekenler

- **Boş Çalışma Sorunu Çözümü:**  
  Orijinal cihazda demleme ünitesinin rezistansı sabit 7 dakika çalışıyordu.  
  Bu, suyun tamamen bitmesi durumunda cihazın zarar görmesine ve enerji israfına yol açıyordu.  
  Yeni yazılım bu sorunu çözüyor. Su bittiğinde rezistans kapanır ve cihaz korunur.

- **Demleme Süresi ve Seviyeleri:**  
  Demleme seviyesine bağlı olarak belirlenen süre sonunda rezistans kapatılarak cihaz korunur.  
  Kullanıcıdan bağımsız olarak enerji tasarrufu ve uzun ömürlü kullanım sağlanır.

---

## 🌍 Projeyi Geliştirme Amacı

Bu projeyi açık kaynak olarak paylaşmamın temel amacı, kullanıcıların ihtiyaçlarına göre geri bildirimler almak ve yazılımı daha da geliştirmektir. Uzun vadede tam stabil bir akıllı kettle/çaycı oluşturmayı hedefliyorum.

---

## 🛠️ Katkıda Bulunun

Kullanıcılar sorun bildirerek veya projeyi fork edip **Pull Request (PR)** göndererek yazılım tarafında katkı sağlayabilirler.  
**Katkılarınız için teşekkürler!**  
Sorularınız veya önerileriniz için lütfen [GitHub Issues](https://github.com/omerfaruk-aran/caysever_robotea/issues) sayfasını kullanın.
