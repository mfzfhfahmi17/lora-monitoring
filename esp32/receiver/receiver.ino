/*
  ============================================================
  ESP32 RECEIVER / GATEWAY LORA - DARAT
  ============================================================
  Fungsi:
  - Menerima paket LoRa dari transmitter kapal
  - Memvalidasi dan memisahkan payload
  - Mengirim ACK_OK ke transmitter
  - Mengirim telemetry ke backend Node.js melalui Wi-Fi/HTTP
  - Backend menyimpan data ke MySQL dan menampilkan dashboard

  Format payload:
  KAPAL_01|Waktu|Latitude|Longitude|Status|Sudut|Jarak

  Library:
  - LoRa by Sandeep Mistry
  - ArduinoJson
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>

// ============================================================
// WIFI DAN BACKEND
// ============================================================
const char* WIFI_SSID = "GanGan";
const char* WIFI_PASSWORD = "morgan23";

// Gunakan IPv4 komputer yang menjalankan backend.
// Jangan gunakan localhost atau 127.0.0.1 pada ESP32.
const char* API_URL = "http://10.86.219.207:3000/api/telemetry";

// Harus sama persis dengan DEVICE_API_KEY pada file .env backend.
const char* DEVICE_API_KEY =
  "jRM1hnJkvWX69iXapQNTgiL25czVrX-zuOSBlnPGyFw5G6NQgaxUnAhe3DCC9bxw";

// ============================================================
// PIN SPI DAN LORA
// ============================================================
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_SS     5
#define LORA_RST   14
#define LORA_DIO0  26

// ============================================================
// KONFIGURASI LORA
// HARUS SAMA DENGAN TRANSMITTER
// ============================================================
const long LORA_FREQUENCY = 915E6;
const byte LORA_SYNC_WORD = 0xF3;
const int LORA_SPREADING_FACTOR = 7;
const long LORA_BANDWIDTH = 125E3;
const int LORA_CODING_RATE = 5;
const int LORA_TX_POWER = 17;

// ============================================================
// KONFIGURASI SISTEM
// ============================================================
const unsigned long SIGNAL_TIMEOUT_MS = 12000;
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long HTTP_TIMEOUT_MS = 5000;

unsigned long lastPacketTime = 0;
unsigned long lastWifiAttempt = 0;
bool pernahTerimaLoRa = false;
bool statusWiFiSebelumnya = false;

// ============================================================
// FORMAT UPTIME RECEIVER
// ============================================================
String getUptime() {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  char buffer[16];
  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02d:%02d",
    hours,
    minutes,
    seconds
  );

  return String(buffer);
}

// ============================================================
// MEMULAI / MENGULANG KONEKSI WIFI TANPA MENUNGGU LAMA
// ============================================================
void mulaiKoneksiWiFi() {
  Serial.print("Menghubungkan ke Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
}

void updateStatusWiFi(unsigned long now) {
  bool wifiTersambung = WiFi.status() == WL_CONNECTED;

  if (wifiTersambung != statusWiFiSebelumnya) {
    statusWiFiSebelumnya = wifiTersambung;

    if (wifiTersambung) {
      Serial.println();
      Serial.println("Wi-Fi terhubung.");
      Serial.print("IP receiver : ");
      Serial.println(WiFi.localIP());
      Serial.print("API backend : ");
      Serial.println(API_URL);
    } else {
      Serial.println();
      Serial.println("Wi-Fi terputus. Receiver tetap menerima paket LoRa.");
    }
  }

  if (!wifiTersambung && now - lastWifiAttempt >= WIFI_RETRY_MS) {
    mulaiKoneksiWiFi();
  }
}

// ============================================================
// NORMALISASI STATUS
// ============================================================
bool normalisasiStatus(String& status) {
  status.trim();
  status.toUpperCase();

  if (status == "ADA") {
    return true;
  }

  if (
    status == "TIDAK" ||
    status == "TIDAK ADA" ||
    status == "TIDAK_ADA" ||
    status == "TIDAKADA" ||
    status == "AMAN"
  ) {
    status = "TIDAK ADA";
    return true;
  }

  return false;
}

// ============================================================
// PARSING PAYLOAD
// ============================================================
bool parsePayload(
  const String& payload,
  String& nodeId,
  String& senderTime,
  double& latitude,
  double& longitude,
  String& trashStatus,
  float& servoAngle,
  float& objectDistanceCm
) {
  int separator[6];
  int searchFrom = 0;

  for (int i = 0; i < 6; i++) {
    separator[i] = payload.indexOf('|', searchFrom);

    if (separator[i] < 0) {
      return false;
    }

    searchFrom = separator[i] + 1;
  }

  nodeId = payload.substring(0, separator[0]);
  senderTime = payload.substring(separator[0] + 1, separator[1]);

  String latitudeText =
    payload.substring(separator[1] + 1, separator[2]);

  String longitudeText =
    payload.substring(separator[2] + 1, separator[3]);

  trashStatus =
    payload.substring(separator[3] + 1, separator[4]);

  String servoText =
    payload.substring(separator[4] + 1, separator[5]);

  String distanceText =
    payload.substring(separator[5] + 1);

  nodeId.trim();
  senderTime.trim();
  latitudeText.trim();
  longitudeText.trim();
  servoText.trim();
  distanceText.trim();

  if (
    nodeId.length() == 0 ||
    latitudeText.length() == 0 ||
    longitudeText.length() == 0 ||
    servoText.length() == 0 ||
    distanceText.length() == 0
  ) {
    return false;
  }

  if (!normalisasiStatus(trashStatus)) {
    return false;
  }

  latitude = latitudeText.toDouble();
  longitude = longitudeText.toDouble();
  servoAngle = servoText.toFloat();
  objectDistanceCm = distanceText.toFloat();

  if (latitude < -90.0 || latitude > 90.0) {
    return false;
  }

  if (longitude < -180.0 || longitude > 180.0) {
    return false;
  }

  if (servoAngle < 0.0 || servoAngle > 180.0) {
    return false;
  }

  if (objectDistanceCm < 0.0) {
    return false;
  }

  return true;
}

// ============================================================
// MENGIRIM ACK KE TRANSMITTER
// ACK DIKIRIM SEBELUM HTTP AGAR TRANSMITTER TIDAK MENUNGGU BACKEND
// ============================================================
void kirimAck() {
  LoRa.idle();
  delay(8);

  LoRa.beginPacket();
  LoRa.print("ACK_OK");
  LoRa.endPacket();

  LoRa.receive();
}

// ============================================================
// KIRIM DATA KE BACKEND
// ============================================================
bool kirimKeBackend(
  const String& rawPayload,
  const String& nodeId,
  const String& senderTime,
  double latitude,
  double longitude,
  const String& trashStatus,
  float servoAngle,
  float objectDistanceCm,
  int rssi
) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Backend dilewati: Wi-Fi belum tersambung.");
    return false;
  }

  StaticJsonDocument<512> document;

  document["nodeId"] = nodeId;
  document["senderTime"] = senderTime;
  document["latitude"] = latitude;
  document["longitude"] = longitude;
  document["trashStatus"] = trashStatus;
  document["servoAngle"] = servoAngle;
  document["objectDistanceCm"] = objectDistanceCm;
  document["rssi"] = rssi;
  document["rawPayload"] = rawPayload;

  String jsonBody;
  serializeJson(document, jsonBody);

  WiFiClient client;
  HTTPClient http;

  http.setConnectTimeout(3000);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, API_URL)) {
    Serial.println("Gagal membuat koneksi HTTP.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", DEVICE_API_KEY);

  int httpCode = http.POST(jsonBody);

  String response;

  if (httpCode > 0) {
    response = http.getString();
  } else {
    response = http.errorToString(httpCode);
  }

  Serial.println();
  Serial.println("---------------- HASIL BACKEND ----------------");
  Serial.print("HTTP code     : ");
  Serial.println(httpCode);
  Serial.print("Respons       : ");
  Serial.println(response);
  Serial.println("------------------------------------------------");

  http.end();

  return httpCode >= 200 && httpCode < 300;
}

// ============================================================
// TAMPILAN DATA PAKET
// ============================================================
void tampilkanPaket(
  const String& nodeId,
  const String& senderTime,
  double latitude,
  double longitude,
  const String& trashStatus,
  float servoAngle,
  float objectDistanceCm,
  int rssi,
  float snr
) {
  String kualitasSinyal;

  if (rssi >= -80) {
    kualitasSinyal = "KUAT";
  } else if (rssi >= -100) {
    kualitasSinyal = "NORMAL";
  } else {
    kualitasSinyal = "LEMAH";
  }

  Serial.println();
  Serial.println("============================================================");
  Serial.println("[ RECEIVER / GATEWAY DARAT ]");
  Serial.println("============================================================");
  Serial.print("Diterima pada : "); Serial.println(getUptime());
  Serial.print("Node ID       : "); Serial.println(nodeId);
  Serial.print("Waktu kirim   : "); Serial.println(senderTime);
  Serial.print("Koordinat     : ");
  Serial.print(latitude, 6);
  Serial.print(", ");
  Serial.println(longitude, 6);
  Serial.print("Google/OSM    : https://www.openstreetmap.org/?mlat=");
  Serial.print(latitude, 6);
  Serial.print("&mlon=");
  Serial.println(longitude, 6);
  Serial.print("Status sampah : "); Serial.println(trashStatus);
  Serial.print("Sudut servo   : "); Serial.print(servoAngle, 0); Serial.println(" derajat");
  Serial.print("Jarak objek   : ");
  if ((int)objectDistanceCm == 999) {
    Serial.println("tidak ada objek / pembacaan tidak valid");
  } else {
    Serial.print(objectDistanceCm, 0); Serial.println(" cm");
  }
  Serial.print("RSSI           : "); Serial.print(rssi); Serial.println(" dBm");
  Serial.print("SNR            : "); Serial.print(snr, 1); Serial.println(" dB");
  Serial.print("Kualitas       : "); Serial.println(kualitasSinyal);
  Serial.println("ACK            : ACK_OK sudah dikirim");
  Serial.println("============================================================");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  mulaiKoneksiWiFi();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("ERROR: Modul LoRa receiver gagal diinisialisasi.");

    while (true) {
      delay(1000);
    }
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.println();
  Serial.println("============================================================");
  Serial.println("RECEIVER / GATEWAY LORA DARAT SIAP");
  Serial.println("Frekuensi   : 915 MHz");
  Serial.println("Sync Word   : 0xF3");
  Serial.println("Format      : ID|Waktu|Lat|Lon|Status|Sudut|Jarak");
  Serial.println("============================================================");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  updateStatusWiFi(now);

  int packetSize = LoRa.parsePacket();

  if (packetSize > 0) {
    String incoming;
    incoming.reserve(packetSize + 8);

    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    incoming.trim();

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    String nodeId;
    String senderTime;
    String trashStatus;

    double latitude = 0.0;
    double longitude = 0.0;
    float servoAngle = 0.0;
    float objectDistanceCm = 0.0;

    bool valid = parsePayload(
      incoming,
      nodeId,
      senderTime,
      latitude,
      longitude,
      trashStatus,
      servoAngle,
      objectDistanceCm
    );

    if (!valid) {
      Serial.println();
      Serial.print("Payload LoRa tidak valid: ");
      Serial.println(incoming);
      LoRa.receive();
      return;
    }

    lastPacketTime = now;
    pernahTerimaLoRa = true;

    kirimAck();

    tampilkanPaket(
      nodeId,
      senderTime,
      latitude,
      longitude,
      trashStatus,
      servoAngle,
      objectDistanceCm,
      rssi,
      snr
    );

    bool backendBerhasil = kirimKeBackend(
      incoming,
      nodeId,
      senderTime,
      latitude,
      longitude,
      trashStatus,
      servoAngle,
      objectDistanceCm,
      rssi
    );

    Serial.print("Penyimpanan   : ");
    Serial.println(
      backendBerhasil
        ? "berhasil masuk backend/database"
        : "belum masuk backend/database"
    );

    LoRa.receive();
  }

  if (
    pernahTerimaLoRa &&
    now - lastPacketTime > SIGNAL_TIMEOUT_MS
  ) {
    pernahTerimaLoRa = false;

    Serial.println();
    Serial.println(
      "PERINGATAN: Tidak ada paket LoRa selama lebih dari 12 detik."
    );
  }

  delay(2);
}
