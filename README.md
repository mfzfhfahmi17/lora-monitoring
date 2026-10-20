# LoRa Monitoring - MySQL + Leaflet + OpenFreeMap

Paket ini menghubungkan:

```text
Node LoRa -> ESP32 Gateway -> HTTP API Node.js -> MySQL/XAMPP
                                      -> Socket.IO -> Dashboard realtime
                                      -> Leaflet + OpenFreeMap
```

Peta tidak membutuhkan Google Maps API key, Map ID, atau billing. Koneksi internet tetap diperlukan untuk mengunduh library dan basemap OpenFreeMap.

## 1. Kebutuhan

- Node.js 18 atau lebih baru.
- XAMPP dengan MySQL aktif.
- ESP32 dan modul LoRa.
- Arduino IDE.
- Library Arduino:
  - LoRa by Sandeep Mistry
  - ArduinoJson by Benoit Blanchon

## 2. Menjalankan backend

Buka XAMPP dan jalankan **MySQL**. Apache hanya diperlukan bila Anda ingin membuka phpMyAdmin.

Buka CMD di folder proyek:

```bat
copy .env.example .env
npm.cmd install
npm.cmd run generate-key
```

Salin key yang dihasilkan ke `.env`:

```env
PORT=3000
ONLINE_TIMEOUT_SECONDS=12
DEVICE_API_KEY=KEY_YANG_BARU_DIBUAT

DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=root
DB_PASSWORD=
DB_NAME=lora_monitoring

SIMULATOR_API_URL=http://127.0.0.1:3000/api/telemetry
SIMULATOR_INTERVAL_MS=3000
```

Untuk XAMPP standar, password user `root` biasanya kosong. Jika MySQL Anda memakai password, isi `DB_PASSWORD`.

Jalankan:

```bat
npm.cmd start
```

Backend otomatis membuat database `lora_monitoring`, tabel `nodes`, dan tabel `telemetry`.

Buka:

```text
http://localhost:3000
```

Tes backend:

```text
http://localhost:3000/api/health
```

## 3. Konfigurasi ESP32

Buka:

```text
esp32/esp32_gateway_lora_mysql_web.ino
```

Ubah:

```cpp
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASSWORD = "PASSWORD_WIFI";
const char* API_URL = "http://192.168.1.10:3000/api/telemetry";
const char* DEVICE_API_KEY = "KEY_YANG_SAMA_DENGAN_ENV";
```

Cari IPv4 komputer dengan:

```bat
ipconfig
```

Gunakan IPv4 komputer pada `API_URL`. Jangan memakai `localhost`, karena dari ESP32 kata tersebut berarti ESP32 itu sendiri.

Format data dari node LoRa harus:

```text
NodeID|WaktuKirim|Latitude|Longitude|Status|Sudut|Jarak
```

Contoh:

```text
NODE-01|00:12:45|-6.597100|106.806000|ADA|90|18.5
```

Status hanya boleh `ADA` atau `TIDAK ADA`.

## 4. Uji tanpa ESP32

Pastikan backend berjalan, lalu buka CMD kedua:

```bat
npm.cmd run simulate
```

Data demo dikirim setiap tiga detik dan langsung muncul di dashboard. Simulator memakai `127.0.0.1`, bukan `localhost`, untuk menghindari kegagalan koneksi IPv6 `::1` pada Windows/Node.js.

## 5. Endpoint backend

- `GET /api/health` - status backend dan database.
- `GET /api/nodes` - kondisi terakhir semua node.
- `GET /api/nodes/:nodeId/history?limit=300` - jalur riwayat node.
- `GET /api/telemetry/latest?limit=100` - telemetry terbaru.
- `GET /api/stats` - ringkasan dashboard.
- `GET /api/export.csv` - ekspor maksimal 10.000 data.
- `POST /api/telemetry` - menerima data ESP32 dengan header `X-Device-Key`.

## 6. Mengatasi error umum


### `Simulator gagal: fetch failed`

Pastikan backend masih berjalan pada CMD pertama:

```bat
npm.cmd start
```

Lalu uji di browser:

```text
http://127.0.0.1:3000/api/health
```

Jika URL tersebut tidak terbuka, perbaiki backend/MySQL terlebih dahulu. Pada `.env`, gunakan:

```env
SIMULATOR_API_URL=http://127.0.0.1:3000/api/telemetry
```

Jangan gunakan `localhost` bila error menunjukkan alamat `::1`.

### `received invalid response: 59`

Proyek lama masih menggunakan driver PostgreSQL `pg` untuk tersambung ke MySQL. Paket ini sudah memakai `mysql2`. Jalankan dari folder proyek baru ini dan pastikan `package.json` tidak berisi `pg`.

### `ECONNREFUSED 127.0.0.1:3306`

MySQL XAMPP belum aktif atau port bukan 3306. Periksa XAMPP `Config -> my.ini`.

### `Access denied for user root`

Password MySQL salah. Sesuaikan `DB_USER` dan `DB_PASSWORD` pada `.env`.

### Dashboard terbuka tetapi peta kosong

Basemap membutuhkan internet. Buka Developer Tools browser (`F12`) dan periksa apakah domain `unpkg.com` atau `tiles.openfreemap.org` diblokir.

### ESP32 mendapat HTTP `401`

`DEVICE_API_KEY` pada ESP32 berbeda dengan nilai pada `.env`. Perbandingan bersifat case-sensitive.

### ESP32 mendapat HTTP `400`

Payload tidak valid. Periksa koordinat, status, sudut, dan format pemisah `|`.

### Web dapat dibuka di komputer tetapi tidak di HP

Gunakan:

```text
http://IPV4_KOMPUTER:3000
```

Pastikan HP dan komputer berada pada Wi-Fi yang sama dan izinkan Node.js melalui Windows Firewall pada jaringan Private.

## 7. Catatan keamanan

- Jangan upload `.env` ke GitHub.
- Buat device key baru untuk setiap instalasi.
- Untuk akses internet/VPS, gunakan HTTPS dan firewall.
- Key yang pernah dibagikan di chat atau screenshot sebaiknya tidak digunakan kembali.


## Sinkronisasi waktu dan auto-refresh

- Waktu dashboard dipaksa ke zona `Asia/Jakarta` dan disinkronkan dengan waktu backend.
- Kolom **Waktu diterima (WIB)** berasal dari backend/database.
- Kolom **Waktu kapal** berasal dari GPS transmitter. Jika GPS belum valid, waktu kapal dapat belum sinkron.
- Socket.IO memperbarui data secara langsung tanpa refresh halaman.
- Polling cadangan setiap 10 detik mengambil ulang data jika event Socket.IO terlewat.
