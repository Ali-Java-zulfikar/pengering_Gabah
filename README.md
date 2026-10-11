# Pengering Gabah — Progres & Panduan Pakai

Dokumen ini merangkum **posisi proyek saat ini** dan **cara menjalankan sistem**, supaya pengerjaan berikutnya (laporan, pengujian hardware, kalibrasi, fitur baru) tidak mulai dari nol.

**Tanggal ringkasan:** 11 Oktober 2026  
**Status:** prototipe firmware ESP32 + aplikasi Android sudah terhubung lewat WiFi (HTTP JSON). APK debug sudah berhasil di-compile.

---

## 1. Progres saat ini

### Sudah selesai

| Bagian | Isi |
|---|---|
| Firmware ESP32 | Baca DHT22, capacitive moisture v1.2, LDR 4-pin; kendali relay lampu pijar |
| Mode otomatis | Pemanas OFF jika kelembapan gabah ≤ target, atau suhu ruang ≥ batas aman |
| Mode manual | Relay bisa dihidupkan/dimatikan dari aplikasi |
| Jaringan | STA (WiFi rumah/kampus) + fallback hotspot `PengeringGabah` |
| API | `GET /status`, `POST /control`, CORS, mDNS `pengering-gabah.local` |
| Penyimpanan ESP | Target, mode, kalibrasi ADC disimpan di Preferences |
| Aplikasi Android (Kotlin + Compose) | Dashboard, grafik live, sakelar pemanas, slider target/suhu, kalibrasi |
| Build | `assembleDebug` sukses |

### Belum dikerjakan (untuk kemajuan berikutnya)

- Ganti `NAMA_WIFI` / `PASSWORD_WIFI` di sketch lalu uji di hardware asli
- Kalibrasi sensor kapasitif terhadap kadar air gabah (oven / moisture meter)
- Uji keamanan rangkaian AC lampu pijar + relay
- Logging riwayat ke database / file (saat ini grafik hanya ~2 menit di RAM aplikasi)
- Notifikasi saat gabah sudah kering
- Autentikasi / penguncian kontrol (siapa pun di LAN bisa kirim perintah)

---

## 2. Struktur folder yang relevan

```
capstone_fik/
├── program_esp/program_esp.ino     # firmware Arduino IDE
├── app/src/main/java/com/example/capstone_fik/
│   ├── MainActivity.kt
│   ├── data/EspDeviceClient.kt     # HTTP ke ESP
│   ├── data/DryerModels.kt
│   └── ui/                         # dashboard, grafik, ViewModel
├── app/src/main/AndroidManifest.xml
└── DOKUMENTASI.md                  # file ini
```

---

## 3. Hardware & wiring

Board: **ESP32 DevKit**. Sensor analog memakai ADC1 (GPIO 34/35) agar tetap jalan saat WiFi aktif.

| Modul | VCC | GND | Sinyal |
|---|---|---|---|
| DHT22 | 3.3V | GND | DATA → GPIO **4** |
| Capacitive soil moisture v1.2 | **3.3V** (jangan 5V) | GND | AOUT → GPIO **34** |
| LDR modul 4-pin | 3.3V | GND | AO → GPIO **35**, DO → GPIO **32** |
| Relay | sesuai modul (3.3V/5V) | GND | IN → GPIO **26** (aktif LOW) |

Lampu pijar hanya melalui **COM/NO relay**. Sisi AC terpisah dari ESP. Pakai isolasi, fuse, dan pastikan tidak menyentuh terminal AC saat uji.

Pin bisa diubah di `program_esp.ino`:

```cpp
const int PIN_DHT = 4;
const int PIN_MOISTURE = 34;
const int PIN_LDR_AO = 35;
const int PIN_LDR_DO = 32;
const int PIN_RELAY = 26;
const bool RELAY_ACTIVE_LOW = true;
```

---

## 4. Cara pakai firmware (Arduino IDE)

1. Install library:
   - **DHT sensor library** (Adafruit)
   - **Adafruit Unified Sensor**
2. Board: **ESP32 Dev Module**, upload speed 115200.
3. Buka `program_esp/program_esp.ino`, ganti:

```cpp
const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";
```

4. Upload, buka **Serial Monitor 115200**.
5. Catat IP yang muncul.
   - WiFi berhasil → IP LAN, contoh `192.168.1.20`
   - WiFi gagal → hotspot `PengeringGabah` / password `12345678`, IP **`192.168.4.1`**
6. Tes cepat di browser HP/PC (satu jaringan):

```
http://<IP-ESP>/status
```

Default otomatis:

- Target kelembapan gabah: **14%** (skala hasil mapping ADC 0–100)
- Batas suhu aman: **50°C**
- Pembacaan sensor: tiap **2 detik**

Logika otomatis:

- Gabah masih basah (kelembapan > target + 1.5%) → pemanas ON
- Gabah ≤ target **atau** suhu ≥ `maxTemperature` → pemanas OFF

---

## 5. Cara pakai aplikasi Android

1. Buka folder proyek `capstone_fik` di **Android Studio**.
2. Pastikan HP dan ESP **satu WiFi**. Jika ESP mode hotspot, sambungkan HP ke `PengeringGabah`.
3. Run aplikasi ke perangkat (minSdk 29).
4. Di layar:
   - Isi IP ESP, atau coba `pengering-gabah.local`
   - Tekan **Hubungkan** (polling status tiap 2 detik)
   - Lihat kartu suhu, RH udara, kelembapan gabah, cahaya LDR
   - Grafik muncul setelah beberapa sampel masuk
   - **Otomatis / Manual** + sakelar pemanas (manual memaksa mode manual)
   - Geser target kelembapan dan batas suhu, lepas jari agar tersimpan ke ESP
   - Isi `dryRaw` / `wetRaw` lalu **Simpan kalibrasi**

### Kalibrasi sensor kapasitif (penting)

Sensor ini **bukan** moisture meter gabah. Nilai % di aplikasi adalah mapping ADC:

- `dryRaw` ≈ pembacaan sensor di udara / gabah kering (biasanya ADC lebih tinggi)
- `wetRaw` ≈ pembacaan saat sangat basah (biasanya ADC lebih rendah)

Langkah praktis:

1. Catat `moistureRaw` saat probe di udara → masukkan ke **dryRaw**
2. Catat `moistureRaw` saat probe di gabah sangat basah / air → **wetRaw**
3. Simpan kalibrasi
4. Nanti bandingkan dengan alat ukur kadar air gabah agar target (misalnya 14%) lebih bermakna

---

## 6. API ESP32 (untuk uji & fitur baru)

Base URL: `http://<IP>:80`

### `GET /status` atau `GET /`

Contoh field JSON:

```json
{
  "ok": true,
  "temperature": 32.4,
  "humidity": 68.2,
  "grainMoisture": 18.5,
  "moistureRaw": 2100,
  "lightLevel": 45.0,
  "lightRaw": 1800,
  "isDark": false,
  "heaterOn": true,
  "mode": "auto",
  "targetMoisture": 14.0,
  "maxTemperature": 50.0,
  "dryRaw": 3000,
  "wetRaw": 1400,
  "wifiRssi": -45,
  "ip": "192.168.1.20",
  "uptime": 120
}
```

### `POST /control`

Header: `Content-Type: application/json`

Contoh body (boleh sebagian):

```json
{ "mode": "manual", "heaterOn": true }
```

```json
{ "mode": "auto", "targetMoisture": 14, "maxTemperature": 50 }
```

```json
{ "dryRaw": 3050, "wetRaw": 1420 }
```

Aplikasi Android memanggil endpoint ini dari `EspDeviceClient.kt`.

---

## 7. Rencana kemajuan ke depan

Urutan yang disarankan:

1. **Uji hardware end-to-end**  
   Semua sensor tampil di aplikasi, relay klik, lampu nyala/mati sesuai mode.
2. **Kalibrasi kadar air**  
   Bandingkan `grainMoisture` dengan moisture meter; catat tabel ADC vs % kadar air; sesuaikan rumus di firmware jika perlu.
3. **Keamanan listrik**  
   Relay berkualitas, housing, jangan uji AC tanpa pengawasan.
4. **Manfaatkan LDR**  
   Saat ini hanya monitoring. Bisa dipakai: kurangi pemanas jika cahaya matahari cukup, atau catat siang/malam di laporan.
5. **Data untuk laporan capstone**  
   Simpan CSV/timestamp di aplikasi atau SD card ESP (grafik, durasi pengeringan, energi).
6. **UX**  
   Notifikasi “gabah kering”, riwayat sesi pengeringan, scan IP otomatis.
7. **Ketahanan**  
   Reconnect WiFi, watchdog, hysteresis suhu yang lebih halus.

---

## 8. Troubleshooting singkat

| Gejala | Cek |
|---|---|
| Aplikasi “Tidak terhubung” | Satu WiFi? IP benar? `usesCleartextTraffic` sudah aktif. Tes `/status` di browser |
| DHT NaN / 0 | Wiring DATA, 3.3V, library Adafruit terpasang |
| Kelembapan gabah aneh | Kalibrasi `dryRaw`/`wetRaw`; VCC sensor 3.3V |
| Relay terbalik | Ubah `RELAY_ACTIVE_LOW` |
| WiFi ESP gagal | Serial menampilkan hotspot; HP join `PengeringGabah`, IP `192.168.4.1` |
| Build Android gagal di terminal | Set `JAVA_HOME` ke JBR Android Studio, atau Run dari Android Studio |

---

## 9. Catatan untuk laporan

Sistem ini memantau lingkungan pengering (suhu, RH, cahaya) dan mengendalikan pemanas berdasarkan **proksi kelembapan** dari sensor kapasitif. Klaim “kadar air gabah 14%” baru valid setelah kalibrasi terhadap alat ukur standar. Mode manual ada sebagai cadangan operasional.
