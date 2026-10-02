"""Home Assistant geçmişinden tekrar oynatma verisi üretir.

Girdi: [["2026-10-02T21:54:20.674+03:00", "ntc_sicaklik", "96.40"], ...] biçiminde JSON (an, varlık kısa adı, durum).
Varlık kısa adları: ntc_sicaklik, mod_durumu, kettle_durumu, cay_tazeligi, aktif_mod. HA'nın
/api/history/period çıktısından birkaç satırla üretilebilir.

Kullanım: python3 fixture_olustur.py <gecmis.json> <çıkış.csv> <başlangıç "YYYY-MM-DD HH:MM:SS"> <bitiş> [komut ...]
  komut: "HH:MM:SS su_kaynatma=on" · "HH:MM:SS su_kaynatma=off" · "HH:MM:SS cay=MAX" · "HH:MM:SS cay=KAPALI"

Çıktı satırları (t_ms = başlangıçtan beri ms):
  T   gerçek sıcaklık okuması (HA'nın kaydettiği an)
  t   dolgu: HA yalnız DEĞİŞEN değeri kaydeder; cihaz 2 sn'de bir okuduğu için aradaki aynı değerli okumalar eklenir
  CMD kullanıcının o anda yaptığı işlem (cihaz tuşu ya da HA) — testte aynı anda uygulanır
  EXP cihazın o anda gerçekte yayınladığı durum — testte karşılaştırma için
Çıktı yalnız sıcaklık, durum ve saat içerir.
"""
import datetime as dt
import json
import sys

TZ = dt.timezone(dt.timedelta(hours=3))
SAMPLE_MS = 2000


def main():
    src, out, a, b = sys.argv[1:5]
    cmds = sys.argv[5:]
    start = dt.datetime.strptime(a, "%Y-%m-%d %H:%M:%S").replace(tzinfo=TZ)
    end = dt.datetime.strptime(b, "%Y-%m-%d %H:%M:%S").replace(tzinfo=TZ)
    rows = [(dt.datetime.fromisoformat(t), e, s) for t, e, s in json.load(open(src))]
    rows = [r for r in rows if start <= r[0] <= end]

    def ms(t):
        return int(round((t - start).total_seconds() * 1000))

    lines = []
    temps = [(t, s) for t, e, s in rows if e == "ntc_sicaklik" and s not in ("unknown", "unavailable")]
    for i, (t, s) in enumerate(temps):
        lines.append((ms(t), 0, "T", s, t))
        nxt = temps[i + 1][0] if i + 1 < len(temps) else None
        if nxt is not None:
            k = 1
            while ms(t) + k * SAMPLE_MS < ms(nxt) - 1000:  # sonraki gerçek okumaya 1 sn'den fazla varsa dolgu
                tt = t + dt.timedelta(milliseconds=k * SAMPLE_MS)
                lines.append((ms(tt), 0, "t", s, tt))
                k += 1
    for t, e, s in rows:
        if e in ("mod_durumu", "kettle_durumu", "cay_tazeligi", "aktif_mod") and s not in ("unknown", "unavailable"):
            lines.append((ms(t), 2, "EXP", f"{e}={s}", t))
    for c in cmds:
        hhmmss, what = c.split(" ", 1)
        t = dt.datetime.strptime(f"{start:%Y-%m-%d} {hhmmss}", "%Y-%m-%d %H:%M:%S").replace(tzinfo=TZ)
        lines.append((ms(t), 1, "CMD", what, t))
    lines.sort(key=lambda x: (x[0], x[1]))
    with open(out, "w") as f:
        f.write(f"# robotea HA geçmişi {start:%Y-%m-%d %H:%M:%S} → {end:%H:%M:%S} (yerel saat). Biçim: tools/fixture_olustur.py\n")
        f.write("t_ms,saat,tur,deger\n")
        for m, _, typ, val, t in lines:
            f.write(f"{m},{t:%H:%M:%S},{typ},{val}\n")
    n = {k: sum(1 for x in lines if x[2] == k) for k in ("T", "t", "CMD", "EXP")}
    print(f"{out}: {n}")


main()
