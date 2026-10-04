#!/bin/sh
# caysever_robotea bileşenini bilgisayarda derleyip bütün senaryoları çalıştırır.
#   ./run.sh                      → depodaki bileşen (../../components/caysever_robotea)
#   SRC=<dizin> ./run.sh          → başka bir kaynak (ör. karşılaştırma için eski sürüm)
#   ./run.sh <senaryo> [-v]       → tek senaryo, -v ile bileşenin kendi kayıt satırları
#   ./run.sh tarama               → yalnız "temiz ısınmada karar" taraması (satır satır; iki sürümün çıktısı diff'lenir)
# Her senaryo ayrı süreçte çalışır (bileşende fonksiyon içi static değişkenler var).
set -u
cd "$(dirname "$0")"
SRC="${SRC:-../../components/caysever_robotea}"
OUT="${OUT:-build/robotea_test}"
mkdir -p "$(dirname "$OUT")"

if ! c++ -std=c++20 -O1 -Wall -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-private-field \
  -Wno-unused-lambda-capture -Wno-unused-function -I stubs -I "$SRC" "$SRC/caysever_robotea.cpp" test_main.cpp -o "$OUT"; then
  echo "DERLEME HATASI"
  exit 2
fi

tarama() {
  # 14 hız × 4 örnekleme fazı × 3 profil = 168 durum
  for prof in d h g; do
    for rate in 1.00 1.20 1.40 1.50 1.60 1.70 1.80 1.90 2.00 2.26 2.50 3.00 4.00 6.00; do
      for phase in 0 500 1000 1500; do
        "$OUT" tarama "$rate" "$phase" "$prof" | grep "tarama hız"
      done
    done
  done
}

if [ $# -ge 1 ] && [ "$1" = "tarama" ]; then
  tarama
  exit 0
fi

if [ $# -ge 1 ]; then
  "$OUT" "$@"
  exit $?
fi

ALL="replay-aksam replay-yeniden replay-1eki az-su yarim-litre kuru az-su-sicrama tek-sicrama ardisik-sicrama toparlanma-adimi \
nan-kaynatirken nan-acilis kritik-mod-yayini kritik-ha-komutu kritik-kisa-nan kritik-kettle-kaldir asiri-isinma \
led-kettle-kaldir led-diger-modlar kaldirilmisken-komut select-yok ota-basliyor acilis-role \
replay-3eki-bos cay-su-bitince cay-bos-hazne cay-algi-yok cay-anahtar-kapali cay-sicak-su-konusma cay-sicak-su-konusma-sureli \
cay-kettle-kaldir-demlerken cay-kettle-kaldir-sureli cay-ust-sinir algi-firtina otomatik-kapanma otomatik-kapanma-yok"
fail=0
passed=0
for s in $ALL; do
  if "$OUT" "$s"; then passed=$((passed + 1)); else fail=$((fail + 1)); fi
done
echo
echo "ÖZET ($SRC): $passed senaryo geçti, $fail senaryo kaldı"
[ "$fail" -eq 0 ]
