#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>

// Firebase
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// FIREBASE SETUP
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
#define API_KEY "AIzaSyB8vZQNPFB3HUUy06Cm4HNI87SS-f6-riY"
#define DATABASE_URL "https://coba-c7eac-default-rtdb.firebaseio.com/"
void firebaseSetInt(String, int);
void firebaseSetString(String, String);
int firebaseGetInt(String);
String firebaseGetString(String);

// Konfigurasi perangkat
RTC_DS3231 rtc;
const int servoPin = 25;
Servo servo1;
const int LOADCELL_DOUT_PIN = 33;
const int LOADCELL_SCK_PIN = 32;
HX711 scale;
const int stepsPerRevolution = 2048;
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

int jumlahIkan = 100;         // Nilai default
int stokPakan = 20;           // Variabel untuk stok pakan
int pemberianPakan = 0;       // Status pemberian pakan

int jam, menit, detik, hari, bulan, tahun;
int startDay = 14, startMonth = 11, startYear = 2024;

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
  scale.set_scale(256.63);
  scale.tare();

  // Inisialisasi Stepper
  myStepper.setSpeed(10);

  // Sambungan awal ke Wi-Fi
  connectWiFi();

  // Firebase
  setupFirebase();

  // Sinkronisasi waktu
  sinkronisasiWaktu();
}

void loop() {
  DateTime now = rtc.now();
  jam = now.hour();
  menit = now.minute();
  detik = now.second();
  hari = now.day();
  bulan = now.month();
  tahun = now.year();

  // Periksa dan lakukan pemberian pakan manual dari Firebase
  if (firebaseGetString("beriPakan") == "1") {
    int pakanGram = hitungPakan(jumlahIkan);
    beriPakan(pakanGram);
    firebaseSetString("beriPakan", "0"); // Kembali ke status default
  }

  // Update jumlah ikan dari Firebase
  jumlahIkan = firebaseGetInt("jumlahIkan");

  // Update hari operasional alat
  firebaseSetInt("hariAlat", hitungHariOperasional());

  // Update stok pakan
  firebaseSetInt("stokPakan", stokPakan);

  // Update pemberian pakan berdasarkan waktu
  if (jam == 10 && menit == 0 && detik == 0) {
    pemberianPakan = 1;
  } else if (jam == 15 && menit == 0 && detik == 0) {
    pemberianPakan = 2;
  } else if (jam == 20 && menit == 0 && detik == 0) {
    pemberianPakan = 3;
  } else if (jam == 23 && menit == 59 && detik == 59) {
    pemberianPakan = 0; // Reset di malam hari
  }
  firebaseSetInt("pemberianPakan", pemberianPakan);

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

  delay(500);
}

// Fungsi Firebase
void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

int firebaseGetInt(String databaseDirectory) {
  if (Firebase.RTDB.getInt(&fbdo, databaseDirectory)) {
    return fbdo.intData();
  } else {
    Serial.println(fbdo.errorReason());
    return 0; // Nilai default jika gagal
  }
}

String firebaseGetString(String databaseDirectory) {
  if (Firebase.RTDB.getString(&fbdo, databaseDirectory)) {
    return fbdo.stringData();
  } else {
    Serial.println(fbdo.errorReason());
    return ""; // Nilai default jika gagal
  }
}

void firebaseSetInt(String databaseDirectory, int value) {
  if (!Firebase.RTDB.setInt(&fbdo, databaseDirectory, value)) {
    Serial.println("Gagal mengirim data: " + fbdo.errorReason());
  }
}

void firebaseSetString(String databaseDirectory, String value) {
  if (!Firebase.RTDB.setString(&fbdo, databaseDirectory, value)) {
    Serial.println("Gagal mengirim data: " + fbdo.errorReason());
  }
}

// Fungsi untuk menghitung hari operasional alat
int hitungHariOperasional() {
  DateTime now = rtc.now();
  return (now - DateTime(startYear, startMonth, startDay)).days();
}

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

// Fungsi pemberian pakan
void beriPakan(int gram) {
  servo1.write(18);
  delay(1000);
  while (scale.get_units() < gram) {
    delay(100);
  }
  servo1.write(0);
  delay(1000);
  kosongkanTempatPakan();
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

// Fungsi untuk koneksi Wi-Fi
void connectWiFi() {
  WiFiManager wifiManager;
  if (WiFi.status() == WL_CONNECTED) return;
  wifiManager.autoConnect("Alat Pakan Lele");
}

// Sinkronisasi waktu dengan NTP
void sinkronisasiWaktu() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
}
