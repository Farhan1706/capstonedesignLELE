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

// Firebase
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h" // Provide the token generation process info.
#include "addons/RTDBHelper.h" // Provide the RTDB payload printing info and other helper functions.
// FIREBASE SETUP
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
#define API_KEY "AIzaSyB8vZQNPFB3HUUy06Cm4HNI87SS-f6-riY"
#define DATABASE_URL "https://coba-c7eac-default-rtdb.firebaseio.com/"
void firebaseSetInt(String, int);
void firebaseSetFloat(String, float);
void firebaseSetString(String databaseDirectory, String value);
String firebaseGetString(String databaseDirectory);
String device_root = "/";


// Konfigurasi Servo
const int servoPin = 25; // Pin Servo diperbarui
Servo servo1;

// Konfigurasi Loadcell
const int LOADCELL_DOUT_PIN = 33; // Pin Loadcell diperbarui
const int LOADCELL_SCK_PIN = 32; // Pin Loadcell diperbarui
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
unsigned long previousMillisServo = 0; // Variabel untuk menyimpan waktu terakhir servo bergerak
const unsigned long intervalServo = 5000; // Interval waktu 5 detik
bool servoState = false; // Status untuk menentukan posisi servo

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
  servo1.write(0);
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
  if (detik == 0 || detik == 31) {
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

  // Atur waktu tunggu untuk koneksi otomatis (30 detik)
  wifiManager.setTimeout(15); // Batas waktu dalam detik

  // Coba hubungkan ke Wi-Fi menggunakan WiFiManager
  Serial.println("Menghubungkan ke Wi-Fi menggunakan WiFiManager...");
  if (!wifiManager.autoConnect("Alat Pakan Lele")) { // Nama hotspot AP
    Serial.println("Tidak ada koneksi. Mematikan mode AP.");
    WiFi.mode(WIFI_OFF); // Matikan Wi-Fi
    btStop(); // Jika Bluetooth aktif, matikan untuk menghemat daya (opsional)
  } else {
    // Jika berhasil terhubung, tampilkan alamat IP
    Serial.println("Terhubung ke Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Sinkronkan waktu menggunakan NTP
    configTime(25200, 0, "0.id.pool.ntp.org");  // Atur NTP hanya saat Wi-Fi terhubung
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
  float beratIkan = (hariBerjalan <= 10) ? 1 : 
                    (hariBerjalan <= 20) ? 2 : 
                    (hariBerjalan <= 40) ? 3.5 : 
                    (hariBerjalan <= 50) ? 5.5 : 
                    (hariBerjalan <= 60) ? 15 : 
                    (hariBerjalan <= 70) ? 35 : 
                    (hariBerjalan <= 80) ? 75 : 
                    (hariBerjalan <= 120) ? 90 : 100;

  // Tentukan persentase pakan berdasarkan hari operasional
  float persentasePakan = (hariBerjalan <= 10) ? 0.10 : 
                          (hariBerjalan <= 20) ? 0.09 : 
                          (hariBerjalan <= 40) ? 0.07 : 
                          (hariBerjalan <= 50) ? 0.06 : 
                          (hariBerjalan <= 60) ? 0.05 : 
                          (hariBerjalan <= 70) ? 0.045 : 
                          (hariBerjalan <= 80) ? 0.037 : 
                          (hariBerjalan <= 120) ? 0.025 : 0.02;

  // Hitung jumlah pakan per ikan
  float pakanPerIkan = beratIkan * persentasePakan;

  // Total pakan untuk semua ikan
  int totalPakan = pakanPerIkan * jumlahIkan;
  return totalPakan;
}

// Fungsi untuk memberikan pakan dengan servo dan loadcell
void beriPakan(int gram) {
  Serial.println("Mulai proses pemberian pakan...");

  // Gerakkan servo ke posisi 18 derajat
  Serial.println("Menggerakkan servo ke posisi 18 derajat...");
  servo1.write(18); 
  servo1.write(18); 
  delay(1000); // Tunggu servo mencapai posisi

  // Mulai memonitor berat pakan yang turun
  Serial.println("Memulai monitoring berat...");
  bool beratCukup = false;

  while (!beratCukup) {
    // Baca berat dari load cell
    float beratSaatIni = scale.get_units(10); // Menggunakan rata-rata 10 pembacaan untuk stabilitas
    Serial.print("Berat saat ini: ");
    Serial.println(beratSaatIni);

    // Periksa apakah berat mencukupi
    if (beratSaatIni >= gram) {
      beratCukup = true;
    }
  }

  // Kembalikan servo ke posisi awal
  Serial.println("Berat mencukupi. Mengembalikan servo ke posisi awal...");
  servo1.write(0);
  servo1.write(0);
  delay(1000); // Tunggu servo kembali ke posisi awal

  // Kosongkan tempat pakan dengan motor stepper
  Serial.println("Mengosongkan tempat pakan...");
  kosongkanTempatPakan();

  Serial.println("Proses pemberian pakan selesai.");
}


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