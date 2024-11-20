#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiManager.h>  // Menambahkan pustaka WiFiManager
#include <time.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>

// Pengaturan NTP dan data
RTC_DS3231 rtc;
Preferences preferences;

// Konfigurasi Servo
static const int servoPin = 33; // Pin Servo diperbarui
Servo servo1;

// Konfigurasi Loadcell
const int LOADCELL_DOUT_PIN = 25; // Pin Loadcell diperbarui
const int LOADCELL_SCK_PIN = 26; // Pin Loadcell diperbarui
HX711 scale;

// Konfigurasi Stepper
const int stepsPerRevolution = 2048; 
#define IN1 13 // Pin Stepper diperbarui
#define IN2 12 // Pin Stepper diperbarui
#define IN3 14 // Pin Stepper diperbarui
#define IN4 27 // Pin Stepper diperbarui
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// Variabel waktu dan pengaturan pakan
int jam, menit, detik, hari, bulan, tahun;
int startDay = 14, startMonth = 11, startYear = 2024; // Tanggal awal penggunaan alat
int jumlahIkan = 100;
bool sinkronisasiSiap = false;  // Penanda untuk mencoba koneksi 30 menit sebelum sinkronisasi

void setup() {
  Serial.begin(115200);

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("RTC tidak ditemukan!");
    while (1);
  }

  // Inisialisasi Servo
  servo1.attach(servoPin);
  servo1.write(0);
  
  // Inisialisasi Loadcell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(256.63); // Sesuaikan faktor kalibrasi
  scale.tare();  // Setel loadcell ke nol

  // Inisialisasi Stepper
  myStepper.setSpeed(10); // Set kecepatan stepper sesuai kebutuhan

  // Sambungan awal ke Wi-Fi
  connectWiFi();

  // Sinkronisasi RTC ke NTP jika tersedia
  sinkronisasiWaktu();
  scale.tare();
}

void loop() {
  DateTime now = rtc.now();

  // Simpan jam, menit, dan tanggal dari RTC
  jam = now.hour();
  menit = now.minute();
  detik = now.second();
  hari = now.day();
  bulan = now.month();
  tahun = now.year();
  
  // Cek jika waktu saat ini adalah jadwal pemberian pakan
//   if ((jam == 10 || jam == 15 || jam == 20) && menit == 0 && detik == 0) {
  if (detik == 0) {
    int pakanGram = hitungPakan(jumlahIkan);  // Tentukan gramasi sesuai hari operasional
    beriPakan(pakanGram);           // Lakukan proses pemberian pakan
  }

  // Cek jika waktu saat ini pukul 11:30 malam, coba hubungkan ke Wi-Fi untuk sinkronisasi
  if (jam == 23 && menit == 30 && !WiFi.isConnected()) {
    sinkronisasiSiap = true;
    connectWiFi();
  }

  // Sinkronisasi pada RTC tepat pukul 12:00:00
  if (jam == 0 && menit == 0 && now.second() == 0 && sinkronisasiSiap && WiFi.isConnected()) {
    sinkronisasiWaktu();
    sinkronisasiSiap = false; // Set ulang penanda setelah sinkronisasi
  }

  delay(1000); // Tunggu 1 detik sebelum mengecek ulang
}

// Fungsi untuk koneksi Wi-Fi
void connectWiFi() {
  // Membuat objek WiFiManager
  WiFiManager wifiManager;

  // Jika sudah terhubung ke Wi-Fi, tidak perlu melanjutkan
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Sudah terhubung ke Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return;
  }

  // Atur waktu tunggu untuk koneksi otomatis (10 detik)
  wifiManager.setConnectTimeout(10);

  // Coba hubungkan ke Wi-Fi menggunakan WiFiManager
  Serial.println("Menghubungkan ke Wi-Fi menggunakan WiFiManager...");
  wifiManager.autoConnect("Alat Pakan Lele");  // Nama hotspot AP jika Wi-Fi tidak terhubung

  // Jika berhasil terhubung, tampilkan alamat IP
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Terhubung ke Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Sinkronkan waktu menggunakan NTP
    configTime(25200, 0, "0.id.pool.ntp.org");  // Atur NTP hanya saat Wi-Fi terhubung
  } else {
    Serial.println("Gagal terhubung ke Wi-Fi");
  }
}

// Fungsi untuk sinkronisasi waktu dengan NTP dan menyimpan ke RTC
void sinkronisasiWaktu() {
  struct tm timeinfo;
  
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Gagal mendapatkan waktu dari NTP");
    return;
  }

  rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
  Serial.println("RTC berhasil disinkronkan ke waktu NTP");
}

// Fungsi untuk menghitung gram pakan berdasarkan minggu operasional alat
int hitungPakan(int jumlahIkan) {
  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  Serial.print("Hari Berjalan:");
  Serial.println(hariBerjalan);

  // Tentukan berat ikan per ekor berdasarkan hari operasional
  float beratIkan = 0;
  if (hariBerjalan <= 10) beratIkan = 1;
  else if (hariBerjalan <= 20) beratIkan = 2;
  else if (hariBerjalan <= 40) beratIkan = 3.5;
  else if (hariBerjalan <= 50) beratIkan = 5.5;
  else if (hariBerjalan <= 60) beratIkan = 15;
  else if (hariBerjalan <= 70) beratIkan = 35;
  else if (hariBerjalan <= 80) beratIkan = 75;
  else if (hariBerjalan <= 120) beratIkan = 90;
  else beratIkan = 100;

  // Tentukan persentase pakan berdasarkan hari operasional
  float persentasePakan = 0;
  if (hariBerjalan <= 10) persentasePakan = 0.10; // 10% untuk 1-10 hari
  else if (hariBerjalan <= 20) persentasePakan = 0.09; // 9% untuk 10-20 hari
  else if (hariBerjalan <= 40) persentasePakan = 0.07; // 7% untuk 20-40 hari
  else if (hariBerjalan <= 50) persentasePakan = 0.06; // 6% untuk 40-50 hari
  else if (hariBerjalan <= 60) persentasePakan = 0.05; // 5% untuk 50-60 hari
  else if (hariBerjalan <= 70) persentasePakan = 0.045; // 4.5% untuk 60-70 hari
  else if (hariBerjalan <= 80) persentasePakan = 0.037; // 3.7% untuk 70-80 hari
  else if (hariBerjalan <= 120) persentasePakan = 0.025; // 2.5% untuk 80-120 hari
  else persentasePakan = 0.02; // 2% untuk lebih dari 120 hari

  // Hitung jumlah pakan per ikan
  float pakanPerIkan = beratIkan * persentasePakan;

  // Total pakan untuk semua ikan
  int totalPakan = pakanPerIkan * jumlahIkan;
  return totalPakan;
}

// Fungsi untuk memberikan pakan dengan servo dan loadcell
void beriPakan(int gram) {
  servo1.write(18);  // Servo ke posisi 18 derajat untuk menjatuhkan pakan
  delay(500);        // Waktu untuk memastikan pakan mulai jatuh

  // Monitor berat pakan yang turun
  bool beratCukup = false;
  unsigned long lastReadTime = millis();

  while (!beratCukup) {
    // Baca berat setiap 200 ms untuk menghindari konflik
    if (millis() - lastReadTime >= 200) {
      lastReadTime = millis();
      float beratSaatIni = scale.get_units();

      Serial.print("Berat saat ini: ");
      Serial.println(beratSaatIni);

      if (beratSaatIni >= gram) {
        beratCukup = true;
      }
    }
  }

  // Kembalikan servo ke posisi awal
  servo1.write(0);
  delay(500);

  // Kosongkan tempat pakan dengan motor stepper
  kosongkanTempatPakan();
}


// kode asli
// void kosongkanTempatPakan() {
//   unsigned long startTime = millis(); // Catat waktu mulai
//   bool pembersihanSelesai = false;

//   while (!pembersihanSelesai) {
//     float beratSaatIni = scale.get_units(); // Baca berat
//     Serial.print("Berat saat ini: ");
//     Serial.println(beratSaatIni);

//     if (beratSaatIni > 8) {
//       myStepper.step(stepsPerRevolution / 10); // Jalankan stepper
//     } else if (millis() - startTime >= 15000) {
//       pembersihanSelesai = true; // Berhenti setelah 15 detik
//     }

//     delay(100); // Beri jeda antar pembacaan
//   }

//   myStepper.step(0); // Hentikan motor setelah pembersihan selesai
// }

void kosongkanTempatPakan() {
  unsigned long startTime = millis(); // Catat waktu mulai
  bool pembersihanSelesai = false;

  while (!pembersihanSelesai) {
    float beratSaatIni = scale.get_units(); // Baca berat
    Serial.print("Berat saat ini: ");
    Serial.println(beratSaatIni);

    // Jika berat > 8 gram, terus gerakkan motor
    if (beratSaatIni > 8) {
      myStepper.step(stepsPerRevolution / 10); // Jalankan stepper
      startTime = millis(); // Reset waktu saat berat masih tinggi
    } else {
      // Berat sudah ≤ 8 gram, periksa waktu
      if (millis() - startTime < 15000) {
        myStepper.step(stepsPerRevolution / 10); // Motor tetap bergerak
      } else {
        pembersihanSelesai = true; // Selesaikan proses setelah 15 detik
      }
    }

    delay(100); // Beri jeda antar pembacaan
  }

  myStepper.step(0); // Hentikan motor setelah pembersihan selesai
}



//jika eror:
// void kosongkanTempatPakan() {
//     unsigned long startTime = millis(); // Catat waktu mulai
//     bool pembersihanSelesai = false;

//     while (!pembersihanSelesai) {
//         myStepper.step(stepsPerRevolution / 10); // Jalankan stepper
//         delay(100); // Tunggu untuk mengurangi getaran sebelum membaca

//         float beratSaatIni = scale.get_units(); // Baca berat
//         Serial.print("Berat saat ini: ");
//         Serial.println(beratSaatIni);

//         if (beratSaatIni > 8) {
//             startTime = millis(); // Reset waktu saat berat masih tinggi
//         } else if (millis() - startTime >= 15000) {
//             pembersihanSelesai = true; // Berhenti setelah 15 detik
//         }
//     }

//     myStepper.step(0); // Hentikan motor setelah pembersihan selesai
// }
