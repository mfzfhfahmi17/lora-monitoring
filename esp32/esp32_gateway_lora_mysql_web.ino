#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>

// -------------------- WIFI DAN API --------------------
const char *WIFI_SSID = "GanGan";
const char *WIFI_PASSWORD = "morgan23";

// Ganti dengan IPv4 komputer yang menjalankan backend, bukan localhost.
const char *API_URL = "http://10.86.219.207:3000/api/telemetry";

// Harus sama persis dengan DEVICE_API_KEY pada file .env.
const char *DEVICE_API_KEY = "jRM1hnJkvWX69iXapQNTgiL25czVrX-zuOSBlnPGyFw5G6NQgaxUnAhe3DCC9bxw";

// -------------------- PIN LORA ESP32 --------------------
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 26

const long LORA_FREQUENCY = 915E6;
const byte LORA_SYNC_WORD = 0xF3;

unsigned long lastPacketTime = 0;
unsigned long lastWifiAttempt = 0;
const unsigned long SIGNAL_TIMEOUT_MS = 12000;
const unsigned long WIFI_RETRY_MS = 10000;
bool isLoRaConnected = false;

String getUptime()
{
  unsigned long totalSeconds = millis() / 1000;
  unsigned long hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02lu:%02d:%02d", hours, minutes, seconds);
  return String(buffer);
}

void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.print("Menghubungkan WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println();
    Serial.println("WiFi terhubung.");
    Serial.print("IP gateway: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println();
    Serial.println("WiFi belum terhubung. Gateway tetap menerima LoRa.");
  }
}

bool parsePayload(
    const String &payload,
    String &nodeId,
    String &senderTime,
    double &latitude,
    double &longitude,
    String &trashStatus,
    float &servoAngle,
    float &objectDistanceCm)
{
  int separator[6];
  int searchFrom = 0;

  for (int i = 0; i < 6; i++)
  {
    separator[i] = payload.indexOf('|', searchFrom);
    if (separator[i] < 0)
      return false;
    searchFrom = separator[i] + 1;
  }

  nodeId = payload.substring(0, separator[0]);
  senderTime = payload.substring(separator[0] + 1, separator[1]);
  String latText = payload.substring(separator[1] + 1, separator[2]);
  String lonText = payload.substring(separator[2] + 1, separator[3]);
  trashStatus = payload.substring(separator[3] + 1, separator[4]);
  String servoText = payload.substring(separator[4] + 1, separator[5]);
  String distanceText = payload.substring(separator[5] + 1);

  nodeId.trim();
  senderTime.trim();
  latText.trim();
  lonText.trim();
  trashStatus.trim();
  servoText.trim();
  distanceText.trim();
  trashStatus.toUpperCase();

  if (nodeId.length() == 0 || latText.length() == 0 || lonText.length() == 0)
  {
    return false;
  }

  latitude = latText.toDouble();
  longitude = lonText.toDouble();
  servoAngle = servoText.toFloat();
  objectDistanceCm = distanceText.toFloat();

  if (latitude < -90.0 || latitude > 90.0)
    return false;
  if (longitude < -180.0 || longitude > 180.0)
    return false;
  if (trashStatus != "ADA" && trashStatus != "TIDAK ADA")
    return false;

  return true;
}

void sendAck()
{
  LoRa.idle();
  delay(8);
  LoRa.beginPacket();
  LoRa.print("ACK_OK");
  LoRa.endPacket();
  LoRa.receive();
}

bool sendToBackend(
    const String &rawPayload,
    const String &nodeId,
    const String &senderTime,
    double latitude,
    double longitude,
    const String &trashStatus,
    float servoAngle,
    float objectDistanceCm,
    int rssi)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Data tidak dikirim ke backend: WiFi terputus.");
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
  http.setTimeout(5000);

  if (!http.begin(client, API_URL))
  {
    Serial.println("Gagal memulai koneksi HTTP.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", DEVICE_API_KEY);

  int httpCode = http.POST(jsonBody);
  String response = httpCode > 0 ? http.getString() : http.errorToString(httpCode);

  Serial.print("HTTP status: ");
  Serial.println(httpCode);
  Serial.print("Respons backend: ");
  Serial.println(response);

  http.end();
  return httpCode >= 200 && httpCode < 300;
}

void printPacket(
    const String &nodeId,
    const String &senderTime,
    double latitude,
    double longitude,
    const String &trashStatus,
    float servoAngle,
    float objectDistanceCm,
    int rssi)
{
  Serial.println();
  Serial.println("============================================================");
  Serial.print("Diterima pada : ");
  Serial.println(getUptime());
  Serial.print("Node ID        : ");
  Serial.println(nodeId);
  Serial.print("Waktu kirim    : ");
  Serial.println(senderTime);
  Serial.print("Koordinat      : ");
  Serial.print(latitude, 6);
  Serial.print(", ");
  Serial.println(longitude, 6);
  Serial.print("Status sampah  : ");
  Serial.println(trashStatus);
  Serial.print("Sudut servo    : ");
  Serial.print(servoAngle, 0);
  Serial.println(" derajat");
  Serial.print("Jarak objek    : ");
  Serial.print(objectDistanceCm, 1);
  Serial.println(" cm");
  Serial.print("RSSI            : ");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println("============================================================");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  connectWiFi();

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("ERROR: Modul LoRa gagal diinisialisasi.");
    while (true)
      delay(1000);
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.receive();

  Serial.println();
  Serial.println("PUSAT KENDALI MONITORING LORA SIAP");
  Serial.println("Format: ID|WaktuKirim|Lat|Lon|Status|Sudut|Jarak");
}

void loop()
{
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWifiAttempt >= WIFI_RETRY_MS)
  {
    lastWifiAttempt = now;
    connectWiFi();
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize > 0)
  {
    String incoming;
    incoming.reserve(packetSize + 8);
    while (LoRa.available())
      incoming += (char)LoRa.read();

    int rssi = LoRa.packetRssi();
    lastPacketTime = now;
    isLoRaConnected = true;

    String nodeId;
    String senderTime;
    String trashStatus;
    double latitude = 0;
    double longitude = 0;
    float servoAngle = 0;
    float objectDistanceCm = 0;

    if (!parsePayload(
            incoming,
            nodeId,
            senderTime,
            latitude,
            longitude,
            trashStatus,
            servoAngle,
            objectDistanceCm))
    {
      Serial.print("Payload LoRa tidak valid: ");
      Serial.println(incoming);
      LoRa.receive();
      return;
    }

    // ACK dikirim lebih dulu agar node pengirim tidak menunggu proses HTTP.
    sendAck();

    printPacket(
        nodeId,
        senderTime,
        latitude,
        longitude,
        trashStatus,
        servoAngle,
        objectDistanceCm,
        rssi);

    sendToBackend(
        incoming,
        nodeId,
        senderTime,
        latitude,
        longitude,
        trashStatus,
        servoAngle,
        objectDistanceCm,
        rssi);
  }

  if (isLoRaConnected && now - lastPacketTime > SIGNAL_TIMEOUT_MS)
  {
    isLoRaConnected = false;
    Serial.println("PERINGATAN: Sinyal LoRa terputus lebih dari 12 detik.");
  }
}
