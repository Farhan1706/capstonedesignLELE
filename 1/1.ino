#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>

// Pengaturan Wi-Fi dan NTP
const char* ssid = "RUMAH";
const char* password = "HomeC7A4";
RTC_DS3231 rtc;

// Konfigurasi Servo
static const int servoPin = 13;
Servo servo1;

// Konfigurasi Loadcell
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;
HX711 scale;

// Konfigurasi Stepper
const int stepsPerRevolution = 2048; 
#define IN1 19
#define IN2 18
#define IN3 5
#define IN4 17
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// Variabel waktu dan pengaturan pakan
int jam, menit, detik, hari, bulan, tahun;
int startDay = 14, startMonth = 11, startYear = 2024; // Tanggal awal penggunaan alat
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
  if ((jam == 10 || jam == 15 || jam == 20) && menit == 0 && detik == 0)) {
    int pakanGram = hitungPakan();  // Tentukan gramasi sesuai hari operasional
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
  WiFi.begin(ssid, password);
  Serial.println("Menghubungkan ke Wi-Fi...");
  unsigned long startAttemptTime = millis();

  // Menunggu koneksi hingga 10 detik
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nTerhubung ke Wi-Fi");
    configTime(25200, 0, "0.id.pool.ntp.org");  // Atur NTP hanya saat Wi-Fi terhubung
  } else {
    Serial.println("\nGagal terhubung ke Wi-Fi");
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
int hitungPakan() {
  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  Serial.print("Hari Berjalan:");
  Serial.println(hariBerjalan);

  if (hariBerjalan <= 10) return 10;
  else if (hariBerjalan <= 20) return 20;
  else if (hariBerjalan <= 40) return 35;
  else if (hariBerjalan <= 60) return 100;
  else if (hariBerjalan <= 70) return 300;
  else if (hariBerjalan <= 80) return 500;
  else if (hariBerjalan <= 120) return 800;
  else return 1000;
}

// Fungsi untuk memberikan pakan dengan servo dan loadcell
void beriPakan(int gram) {
  servo1.write(18);  // Servo ke posisi 18 derajat untuk menjatuhkan pakan
  delay(500);        // Waktu untuk memastikan pakan mulai jatuh

  // Monitor berat pakan yang turun, mengurangi 15 dari hasil pembacaan
  while (scale.get_units() < gram) {
    delay(100);
  }

  // Kembalikan servo ke posisi awal
  servo1.write(0);
  delay(500);

  // Kosongkan tempat pakan dengan motor stepper
  kosongkanTempatPakan();
}

// Fungsi untuk mengosongkan tempat pakan hingga loadcell < 10 gram
void kosongkanTempatPakan() {
  // Kosongkan tempat pakan dengan motor stepper hingga berat pakan turun di bawah 10 gram
  while (scale.get_units() > 0.2) { 
    myStepper.step(stepsPerRevolution / 10);  // Gerakkan motor sedikit demi sedikit
    delay(100);  // Beri waktu pembacaan
  }

  delay(10000);  // Motor tetap berputar selama 10 detik setelah mencapai 10 gram
  myStepper.step(0);  // Hentikan motor
}
