#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>
#include <time.h>
#include <Wire.h>
#include "RTClib.h"
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// FIREBASE SETUP
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
#define API_KEY "AIzaSyDnoJO-BrVCBfJMHEAo1uTuBUw86IrzGIA"
#define DATABASE_URL "https://pakanlele-92a8f-default-rtdb.firebaseio.com/"
void firebaseSetInt(String, int);
void firebaseSetString(String, String);
int firebaseGetInt(String);
String firebaseGetString(String);

// Konfigurasi pin
const int LOADCELL_DOUT_PIN = 33; // Pin data HX711
const int LOADCELL_SCK_PIN = 32;  // Pin clock HX711
HX711 scale;
const int servoPin = 25;
Servo servo1;

// Stepper
const int stepsPerRevolution = 2048;
#define IN1 13
#define IN2 12
#define IN3 14
#define IN4 27
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);

// Faktor kalibrasi load cell
const float calibration_factor = 256.63;

// RTC
RTC_DS3231 rtc;

// Variabel utama
int stokPakan = 0;
int jumlahIkan = 100;
int startYear, startMonth, startDay;

void setup() {
  Serial.begin(115200);

  // Koneksi Wi-Fi
  connectWiFi();

  // Firebase setup
  setupFirebase();

  // RTC setup
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi!");
    while (1);
  }

  // Servo
  servo1.attach(servoPin);
  servo1.write(0);

  // Load Cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  // Stepper
  myStepper.setSpeed(10);

  // Ambil data awal dari Firebase
  String jumlahIkanString;

  // Ambil data dari Firebase
  if (Firebase.RTDB.getString(&fbdo, "/jumlahIkan")) {
      jumlahIkanString = fbdo.stringData();
      jumlahIkanString.replace("\"", ""); // Hapus petik
      jumlahIkan = jumlahIkanString.toInt(); // Konversi menjadi integer
      
      Serial.print("Jumlah ikan: ");
      Serial.println(jumlahIkan);
  } else {
      Serial.print("Gagal mendapatkan data jumlah ikan: ");
      Serial.println(fbdo.errorReason());
  }

  String startAlat = firebaseGetString("startAlat");
  parseStartDate(startAlat);
}

void loop() {
  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  int jam = now.hour();
  int menit = now.minute();
  int detik = now.second();

  // Pemberian Pakan Otomatis
  if ((jam == 9 || jam == 15 || jam == 21 || (jam == 0 && menit == 1)) && detik == 0) {
    int pemberianPakan = (jam == 9) ? 1 : (jam == 15) ? 2 : (jam == 21) ? 3 : 4;
    firebaseSetInt("pemberianPakan", pemberianPakan);

    int totalPakan = hitungPakan(jumlahIkan);
    beriPakan(totalPakan);
  }

  // Pemberian Pakan Manual
  if (firebaseGetString("beriPakan") == "1") {
    int totalPakan = hitungPakan(jumlahIkan);
    beriPakan(totalPakan);
    firebaseSetInt("beriPakan", 0); // Reset beriPakan
  }

  // Update hari alat ke Firebase
  firebaseSetInt("hariAlat", hariBerjalan);

  // Update stok pakan ke Firebase
  firebaseSetInt("stokPakan", stokPakan);

  delay(1000);
}

void beriPakan(int gram) {
  Serial.println("Proses pemberian pakan dimulai...");
  servo1.write(18);
  delay(1000);

  bool beratCukup = false;
  while (!beratCukup) {
    float beratSaatIni = scale.get_units(10);
    if (beratSaatIni >= gram) beratCukup = true;
  }

  servo1.write(0);
  delay(1000);

  kosongkanTempatPakan();
  Serial.println("Pemberian pakan selesai.");
}

void kosongkanTempatPakan() {
  unsigned long startTime = millis();
  bool pembersihanSelesai = false;

  while (!pembersihanSelesai) {
    float beratSaatIni = scale.get_units();
    if (beratSaatIni > 8) {
      myStepper.step(stepsPerRevolution / 10);
      startTime = millis();
    } else if (millis() - startTime < 15000) {
      myStepper.step(stepsPerRevolution / 10);
    } else {
      pembersihanSelesai = true;
    }
  }
  if (pembersihanSelesai) {
    myStepper.step(0); // Pastikan motor berhenti
  }
  myStepper.step(0);
}

int hitungPakan(int jumlahIkan) {
  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  float beratIkan = (hariBerjalan <= 10) ? 1 : 
                    (hariBerjalan <= 20) ? 2 : 
                    (hariBerjalan <= 40) ? 3.5 : 
                    (hariBerjalan <= 50) ? 5.5 : 
                    (hariBerjalan <= 60) ? 15 : 
                    (hariBerjalan <= 70) ? 35 : 
                    (hariBerjalan <= 80) ? 75 : 
                    (hariBerjalan <= 120) ? 90 : 100;

  float persentasePakan = (hariBerjalan <= 10) ? 0.10 : 
                          (hariBerjalan <= 20) ? 0.09 : 
                          (hariBerjalan <= 40) ? 0.07 : 
                          (hariBerjalan <= 50) ? 0.06 : 
                          (hariBerjalan <= 60) ? 0.05 : 
                          (hariBerjalan <= 70) ? 0.045 : 
                          (hariBerjalan <= 80) ? 0.037 : 
                          (hariBerjalan <= 120) ? 0.025 : 0.02;

  float pakanPerIkan = beratIkan * persentasePakan;
  return pakanPerIkan * jumlahIkan;
}

void parseStartDate(String startAlat) {
  startAlat.replace("\"", "");
  startAlat.replace("\\", "");
  int firstSlash = startAlat.indexOf('/');
  int secondSlash = startAlat.lastIndexOf('/');

  startDay = startAlat.substring(0, firstSlash).toInt();
  startMonth = startAlat.substring(firstSlash + 1, secondSlash).toInt();
  startYear = startAlat.substring(secondSlash + 1).toInt();
}

void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

String firebaseGetString(String databaseDirectory) {
  if (Firebase.RTDB.getString(&fbdo, databaseDirectory)) return fbdo.stringData();
  return "";
}

int firebaseGetInt(String databaseDirectory) {
  if (Firebase.RTDB.getInt(&fbdo, databaseDirectory)) return fbdo.intData();
  return 0;
}

void firebaseSetInt(String databaseDirectory, int value) {
  Firebase.RTDB.setInt(&fbdo, databaseDirectory, value);
}

void firebaseSetString(String databaseDirectory, String value) {
  Firebase.RTDB.setString(&fbdo, databaseDirectory, value);
}

void connectWiFi() {
  WiFiManager wifiManager;
  wifiManager.autoConnect("Alat Pakan Lele");
}
