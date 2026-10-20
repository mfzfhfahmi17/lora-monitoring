/*
  ============================================================
  ESP32 TRANSMITTER LORA - NODE KAPAL
  ============================================================
  Fungsi:
  - Membaca GPS NEO-6M
  - Menggerakkan servo radar
  - Membaca sensor ultrasonik HC-SR04
  - Mengirim paket LoRa ke receiver/gateway darat
  - Menunggu balasan ACK_OK dari receiver

  Format payload:
  KAPAL_01|Waktu|Latitude|Longitude|Status|Sudut|Jarak

  Library:
  - LoRa by Sandeep Mistry
  - TinyGPSPlus by Mikal Hart
  - ESP32Servo
*/

#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include <ESP32Servo.h>

// ============================================================
// IDENTITAS NODE
// ============================================================
const char* NODE_ID = "KAPAL_01";

// Jika false, data tidak dikirim sebelum GPS mendapat koordinat valid.
// Untuk uji meja tanpa sinyal GPS, ubah menjadi true dan isi koordinat fallback.
const bool KIRIM_TANPA_FIX_GPS = false;
const double FALLBACK_LATITUDE = -6.597100;
const double FALLBACK_LONGITUDE = 106.806000;

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
// PIN GPS
// GPS TX -> ESP32 RX2 GPIO 16
// GPS RX -> ESP32 TX2 GPIO 17
// ============================================================
#define GPS_RX 16
#define GPS_TX 17

// ============================================================
// PIN KOMPONEN
// ============================================================
#define LED_PIN    2
#define TRIG_PIN  22
#define ECHO_PIN  27
#define SERVO_PIN 15

// ============================================================
// KONFIGURASI LORA
// HARUS SAMA DENGAN RECEIVER
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
const int BATAS_JARAK_CM = 100;
const unsigned long INTERVAL_KIRIM_MS = 5000;
const unsigned long INTERVAL_SERVO_MS = 40;
const unsigned long INTERVAL_JARAK_MS = 120;
const unsigned long ACK_TIMEOUT_MS = 1500;

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
Servo radarServo;

int posisiServo = 90;
int arahServo = 1;
int sudutTerakhir = 90;
int jarakTerakhirCm = 999;

double latitudeTerakhir = 0.0;
double longitudeTerakhir = 0.0;
bool pernahDapatGPS = false;

unsigned long lastSendLoRa = 0;
unsigned long lastServoMove = 0;
unsigned long lastDistanceRead = 0;

// ============================================================
// MEMBACA GPS SECARA TERUS-MENERUS
// ============================================================
void bacaGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid() && gps.location.isUpdated()) {
    latitudeTerakhir = gps.location.lat();
    longitudeTerakhir = gps.location.lng();
    pernahDapatGPS = true;
  }
}

// ============================================================
// WAKTU KIRIM: GPS UTC+7, ATAU UPTIME JIKA WAKTU GPS BELUM VALID
// ============================================================
String getWaktuKirim() {
  char buffer[10];

  // Gunakan waktu GPS hanya jika valid dan datanya masih baru.
  // Uptime tidak dipakai sebagai jam karena akan terlihat tidak sinkron.
  if (gps.time.isValid() && gps.time.age() < 3000) {
    int jamWib = gps.time.hour() + 7;
    if (jamWib >= 24) jamWib -= 24;

    snprintf(
      buffer,
      sizeof(buffer),
      "%02d:%02d:%02d",
      jamWib,
      gps.time.minute(),
      gps.time.second()
    );
  } else {
    snprintf(buffer, sizeof(buffer), "BELUM_SYNC");
  }

  return String(buffer);
}

// ============================================================
// MEMBACA HC-SR04
// PENTING: ECHO HC-SR04 5V, gunakan pembagi tegangan ke ESP32.
// ============================================================
int bacaJarakCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  unsigned long durasi = pulseIn(ECHO_PIN, HIGH, 30000UL);

  if (durasi == 0) {
    return 999;
  }

  float jarak = (durasi * 0.0343f) / 2.0f;

  if (jarak < 3.0f || jarak > 400.0f) {
    return 999;
  }

  return (int)round(jarak);
}

// ============================================================
// GERAK SERVO NON-BLOCKING
// ============================================================
void updateServo(unsigned long now) {
  if (now - lastServoMove < INTERVAL_SERVO_MS) return;
  lastServoMove = now;

  radarServo.write(posisiServo);
  sudutTerakhir = posisiServo;

  posisiServo += arahServo;

  if (posisiServo >= 180) {
    posisiServo = 180;
    arahServo = -1;
  } else if (posisiServo <= 0) {
    posisiServo = 0;
    arahServo = 1;
  }
}

// ============================================================
// MEMBACA JARAK BERKALA
// ============================================================
void updateJarak(unsigned long now) {
  if (now - lastDistanceRead < INTERVAL_JARAK_MS) return;
  lastDistanceRead = now;
  jarakTerakhirCm = bacaJarakCm();
}

// ============================================================
// MENUNGGU ACK DARI RECEIVER
// ============================================================
bool tungguAck() {
  LoRa.receive();

  unsigned long mulai = millis();

  while (millis() - mulai < ACK_TIMEOUT_MS) {
    // Tetap baca GPS agar buffer serial tidak cepat penuh.
    bacaGPS();

    int packetSize = LoRa.parsePacket();

    if (packetSize > 0) {
      String response;
      response.reserve(packetSize + 4);

      while (LoRa.available()) {
        response += (char)LoRa.read();
      }

      response.trim();

      if (response == "ACK_OK") {
        return true;
      }
    }

    delay(5);
  }

  return false;
}

// ============================================================
// MENGIRIM DATA LORA
// ============================================================
void kirimDataLoRa() {
  double latitudeKirim;
  double longitudeKirim;

  if (pernahDapatGPS) {
    latitudeKirim = latitudeTerakhir;
    longitudeKirim = longitudeTerakhir;
  } else if (KIRIM_TANPA_FIX_GPS) {
    latitudeKirim = FALLBACK_LATITUDE;
    longitudeKirim = FALLBACK_LONGITUDE;
  } else {
    Serial.println();
    Serial.println("GPS belum mendapatkan koordinat valid.");
    Serial.println("Data belum dikirim untuk mencegah marker berada di 0,0.");
    return;
  }

  String waktuKirim = getWaktuKirim();

  String statusSampah =
    (jarakTerakhirCm != 999 && jarakTerakhirCm <= BATAS_JARAK_CM)
      ? "ADA"
      : "TIDAK ADA";

  String payload;
  payload.reserve(120);

  payload += NODE_ID;
  payload += "|";
  payload += waktuKirim;
  payload += "|";
  payload += String(latitudeKirim, 6);
  payload += "|";
  payload += String(longitudeKirim, 6);
  payload += "|";
  payload += statusSampah;
  payload += "|";
  payload += String(sudutTerakhir);
  payload += "|";
  payload += String(jarakTerakhirCm);

  LoRa.idle();
  delay(5);

  LoRa.beginPacket();
  LoRa.print(payload);
  int sendResult = LoRa.endPacket();

  bool ackDiterima = false;

  if (sendResult == 1) {
    ackDiterima = tungguAck();
  }

  digitalWrite(LED_PIN, ackDiterima ? HIGH : LOW);

  Serial.println();
  Serial.println("============================================================");
  Serial.println("[ NODE KAPAL - DATA TRANSMITTER ]");
  Serial.println("============================================================");
  Serial.print("Payload       : "); Serial.println(payload);
  Serial.print("Waktu         : "); Serial.println(waktuKirim);
  Serial.print("Koordinat     : ");
  Serial.print(latitudeKirim, 6);
  Serial.print(", ");
  Serial.println(longitudeKirim, 6);
  Serial.print("Satelit       : ");
  if (gps.satellites.isValid()) {
    Serial.println(gps.satellites.value());
  } else {
    Serial.println("belum diketahui");
  }
  Serial.print("Sudut radar   : "); Serial.print(sudutTerakhir); Serial.println(" derajat");
  Serial.print("Jarak objek   : ");
  if (jarakTerakhirCm == 999) {
    Serial.println("tidak ada objek / pembacaan tidak valid");
  } else {
    Serial.print(jarakTerakhirCm); Serial.println(" cm");
  }
  Serial.print("Status sampah : "); Serial.println(statusSampah);
  Serial.print("Status LoRa   : ");
  Serial.println(ackDiterima ? "ACK_OK diterima" : "ACK tidak diterima");
  Serial.println("============================================================");

  LoRa.receive();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(TRIG_PIN, LOW);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  ESP32PWM::allocateTimer(0);
  radarServo.setPeriodHertz(50);
  radarServo.attach(SERVO_PIN, 500, 2400);
  radarServo.write(posisiServo);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("ERROR: Modul LoRa transmitter gagal diinisialisasi.");
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

  Serial.println();
  Serial.println("============================================================");
  Serial.println("NODE TRANSMITTER KAPAL SIAP");
  Serial.println("Node ID     : KAPAL_01");
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

  bacaGPS();
  updateServo(now);
  updateJarak(now);

  if (now - lastSendLoRa >= INTERVAL_KIRIM_MS) {
    lastSendLoRa = now;
    kirimDataLoRa();
  }

  delay(100);
}
