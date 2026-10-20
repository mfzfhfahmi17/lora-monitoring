#include <SPI.h>
#include <LoRa.h>

#define SS      5
#define RST     14
#define DIO0    26

unsigned long lastPacketTime = 0;
const unsigned long timeout = 12000; 
bool isConnected = false;

// Fungsi untuk membuat format waktu Uptime (Gateway)
String getWaktuTerima() {
  unsigned long totalSeconds = millis() / 1000;
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;
  
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);
  return String(timeStr);
}

void setup() {
  Serial.begin(115200);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(915E6)) { 
    Serial.println("❌ ERROR: Modul LoRa gagal diinisialisasi!");
    while (1); 
  }
  LoRa.setSyncWord(0xF3);
  
  Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
  Serial.println("║          🛰️ PUSAT KENDALI MONITORING LORA DARAT 🛰️           ║");
  Serial.println("╚══════════════════════════════════════════════════════════════╝");
}

void loop() {
  int packetSize = LoRa.parsePacket();
  unsigned long now = millis();

  if (packetSize) {
    lastPacketTime = now;
    isConnected = true;
    String incoming = "";
    while (LoRa.available()) { incoming += (char)LoRa.read(); }

    // PARSING DATA: ID|WaktuKirim|Lat|Lon|Status|Sudut|Jarak
    int p1 = incoming.indexOf('|');
    int p2 = incoming.indexOf('|', p1 + 1);
    int p3 = incoming.indexOf('|', p2 + 1);
    int p4 = incoming.indexOf('|', p3 + 1);
    int p5 = incoming.indexOf('|', p4 + 1);
    int p6 = incoming.indexOf('|', p5 + 1);

    if (p1 != -1 && p6 != -1) {
      String nodeID = incoming.substring(0, p1);
      String waktuKirim = incoming.substring(p1 + 1, p2);
      String lat = incoming.substring(p2 + 1, p3);
      String lon = incoming.substring(p3 + 1, p4);
      String statusSmp = incoming.substring(p4 + 1, p5);
      String sudut = incoming.substring(p5 + 1, p6);
      String jarak = incoming.substring(p6 + 1);
      
      statusSmp.trim(); 
      String waktuTerima = getWaktuTerima();
      int rssi = LoRa.packetRssi();

      // Indikator Sinyal Visual
      String signalQuality = (rssi >= -80) ? "KUAT \xF0\x9F\x9F\xA2" : (rssi >= -100) ? "NORMAL \xF0\x9F\x9F\xA1" : "LEMAH \xF0\x9F\x94\xB4";

      // -----------------------------------------------------------
      // TAMPILAN AESTHETIC SERIAL MONITOR (BERSIH)
      // -----------------------------------------------------------
      Serial.println();
      Serial.print("[ \xE2\x8F\xB1\xEF\xB8\x8F DITERIMA PADA: "); Serial.print(waktuTerima);
      Serial.print(" ] ───── [ \xF0\x9F\x93\xA1 SINYAL: "); Serial.print(signalQuality); Serial.println(" ]");

      Serial.println("\n\xF0\x9F\x93\xA6 INFORMASI NODE (PENGIRIM)"); 
      Serial.print(" \xE2\x94\x9C\xE2\x94\x80 \xF0\x9F\x86\x94 Node ID      : "); Serial.println(nodeID); 
      Serial.print(" \xE2\x94\x9C\xE2\x94\x80 \xE2\x8F\xB1\xEF\xB8\x8F Waktu Kirim  : "); Serial.println(waktuKirim);
      Serial.print(" \xE2\x94\x94\xE2\x94\x80 \xF0\x9F\x93\xB6 RSSI (Kekuatan): "); Serial.print(rssi); Serial.println(" dBm"); 

      Serial.println("\n\xF0\x9F\x93\x8D DATA LOKASI GPS"); 
      Serial.print(" \xE2\x94\x9C\xE2\x94\x80 \xF0\x9F\x8C\x90 Koordinat    : "); Serial.print(lat); Serial.print(", "); Serial.println(lon); 
      Serial.print(" \xE2\x94\x94\xE2\x94\x80 \xF0\x9F\x97\xBA\xEF\xB8\x8F G-Maps       : https://maps.google.com/?q="); 
      Serial.print(lat); Serial.print(","); Serial.println(lon);

      Serial.println("\n\xF0\x9F\x97\x91\xEF\xB8\x8F STATUS SENSOR & LINGKUNGAN"); 
      Serial.print(" \xE2\x94\x9C\xE2\x94\x80 \xE2\x9A\x99\xEF\xB8\x8F Sudut Servo  : "); Serial.print(sudut); Serial.println("\xC2\xB0 (Derajat)");
      Serial.print(" \xE2\x94\x9C\xE2\x94\x80 \xF0\x9F\x93\x8F Jarak Objek  : "); Serial.print(jarak); Serial.println(" cm");
      if (statusSmp == "ADA") {
        Serial.println(" \xE2\x94\x94\xE2\x94\x80 \xE2\x9A\xA0\xEF\xB8\x8F Deteksi      : SAMPAH TERDETEKSI (ADA)"); 
      } else {
        Serial.println(" \xE2\x94\x94\xE2\x94\x80 \xE2\x9C\x85 Deteksi      : TIDAK ADA SAMPAH"); 
      }
      
      // Garis penutup
      Serial.println("\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80");

      // Kirim balik ACK
      LoRa.beginPacket();
      LoRa.print("ACK_OK");
      LoRa.endPacket();
    }
  }

  // Peringatan Sinyal Hilang
  if (isConnected && (now - lastPacketTime > timeout)) {
    isConnected = false;
    Serial.println("\n\xE2\x9D\x8C PERINGATAN KRITIS: SINYAL TERPUTUS! \xE2\x9D\x8C");
  }
}
