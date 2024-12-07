#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>
#include <time.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "RTClib.h"
#include <WiFi.h>
#include <ESP32Ping.h>
#include <Preferences.h>

// Sistem Setup
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
#define API_KEY "AIzaSyDnoJO-BrVCBfJMHEAo1uTuBUw86IrzGIA"
#define DATABASE_URL "https://pakanlele-92a8f-default-rtdb.firebaseio.com/"
const char* ssid = "TBM LITERASI_plus";
const char* password = "marimembaca21";

// Konfigurasi pin
const int LOADCELL_DOUT_PIN = 33; // Pin data HX711
const int LOADCELL_SCK_PIN = 32;  // Pin clock HX711
HX711 scale;
const int servoPin = 25;
Servo servo1;
#define TRIGGER_PIN 26   // Pin Trigger
#define ECHO_PIN 35      // Pin Echo

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
Preferences prefs;

// Variabel utama
int stokPakan;
int jumlahIkan = 100;
int startYear = 0, startMonth = 0, startDay = 0;
bool internetTerhubung = false; // Status internet
unsigned long lastInternetCheck = 0; // Waktu pengecekan internet terakhir (ms)
bool modeOffline = true; // Default ke ModeOffline

void setup() {
  Serial.begin(115200);

  // Inisialisasi Preferences
  prefs.begin("startDate", false);

  if (prefs.isKey("startDay") && prefs.isKey("startMonth") && prefs.isKey("startYear")) {
    startDay = prefs.getInt("startDay");
    startMonth = prefs.getInt("startMonth");
    startYear = prefs.getInt("startYear");
    jumlahIkan = prefs.getInt("jumlahIkan");
    Serial.println("Dapat start date dan jumlahIkan dari Preferences.");
  } else {
    Serial.println("Tidak ada data pada Preferences.");
  }

  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  connectWiFi();
  if (internetTerhubung) {
    setupFirebase();
    config.database_url = DATABASE_URL;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    modeOffline = false;
    jumlahIkan = getJumlahIkanFromFirebase();
    prefs.putInt("jumlahIkan", jumlahIkan);
    String startAlat = firebaseGetString("startAlat");
    parseStartDate(startAlat);
  }

  // RTC setup
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi!");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC kehilangan daya. Mengatur ulang waktu ke waktu kompilasi.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  servo1.attach(servoPin);
  servo1.write(0);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  myStepper.setSpeed(10);
}

void loop() {
  // Periksa koneksi internet setiap 5 menit
  if (millis() - lastInternetCheck > 300000) { // Setiap 5 menit
    internetTerhubung = checkInternetConnection();
    lastInternetCheck = millis();
    if (internetTerhubung) {
      Serial.println("Koneksi internet berhasil. Beralih ke ModeOnline.");
      modeOffline = false;
      setupFirebase();
    } else {
      Serial.println("Koneksi internet gagal. Tetap di ModeOffline.");
      modeOffline = true;
    }
  }

  // Sinkronisasi RTC jika online

  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  int jam = now.hour();
  int menit = now.minute();
  int detik = now.second();

  if (!modeOffline && internetTerhubung && jam == 23 && menit == 30) {
    sinkronisasiWaktu();
  }

  if (modeOffline) {
    jalankanModeOffline(hariBerjalan, jam, menit, detik);
  } else {
    jalankanModeOnline(hariBerjalan, jam, menit, detik);
  }



  delay(1000);
}

void jalankanModeOffline(int hariBerjalan, int jam, int menit, int detik) {
  Serial.println("== ModeOffline ==");
  Serial.print("Hari Berjalan: ");
  Serial.println(hariBerjalan);
  jumlahIkan = prefs.getInt("jumlahIkan");

  // Menggunakan data dari Preferences
  Serial.printf("Preferences - Start Date: %02d/%02d/%04d, Jumlah Ikan: %d\n",
              prefs.getInt("startDay"),
              prefs.getInt("startMonth"),
              prefs.getInt("startYear"),
              jumlahIkan);
  Serial.printf("Waktu Saat Ini: %02d:%02d:%02d\n", jam, menit, detik);

  if (detik == 0) {
    if ((jam == 9 && menit == 1) || (jam == 17 && menit == 1)) {
      int totalPakan = hitungPakan(jumlahIkan);
      beriPakan(totalPakan);
    }
  }
}

void jalankanModeOnline(int hariBerjalan, int jam, int menit, int detik) {
  Serial.println("== ModeOnline ==");

  // Update data ke Firebase
  firebaseSetInt("hariAlat", hariBerjalan);

  // Periksa apakah ada perintah pemberian pakan dari Firebase
  String beriPakanCmd = firebaseGetString("beriPakan");
  if (beriPakanCmd == "1") {
    int totalPakan = hitungPakan(jumlahIkan);
    beriPakan(totalPakan);
    firebaseSetInt("beriPakan", 0); // Reset perintah
  }

  // Sinkronisasi start date dengan Firebase jika diperlukan
  String startDate = firebaseGetString("startAlat");
  if (startDate.length() > 0) {
        // Hilangkan tanda kutip jika ada
        startDate.replace("\"", ""); // Remove double quotes

        // Format dan cetak
        startDate.replace("\\/", "/");  // Remove escape for '/'
        startDate.replace("\\", "");    // Remove any backslashes (\\)

        Serial.println("=== Data Tanggal dari Firebase ===");
        Serial.print("Start Date: ");
        Serial.println(startDate);

        // Parse tanggal menjadi hari, bulan, tahun
        startDay = startDate.substring(0, 2).toInt();
        startMonth = startDate.substring(3, 5).toInt();
        startYear = startDate.substring(6, 10).toInt();

        // Print the parsed values from Firebase
        Serial.printf("Start Date (from Firebase): %02d/%02d/%04d\n", startDay, startMonth, startYear);

        // Simpan tanggal ke NVS
        prefs.putInt("startDay", startDay);
        prefs.putInt("startMonth", startMonth);
        prefs.putInt("startYear", startYear);
        prefs.putInt("jumlahIkan", jumlahIkan);
        Serial.println("Tanggal disimpan ke NVS.");

        // Calculate the difference in days between the RTC date and the Firebase start date
        DateTime now = rtc.now();
        
        // Calculate hari berjalan (difference in days)
        DateTime startDateTime(startYear, startMonth, startDay); 
        int hariBerjalan = (now - startDateTime).days();  // Hitung selisih hari
        Serial.print("Hari Berjalan: ");
        Serial.println(hariBerjalan);

        // Print data from both Firebase and Preferences
        Serial.println("\n----- Data from Firebase and Preferences -----");
        // Print the parsed values from Firebase in one line
        Serial.printf("Start Date (from Firebase): %02d/%02d/%04d\n", startDay, startMonth, startYear);

        Serial.printf("Preferences - Start Date: %02d/%02d/%04d, Jumlah Ikan: %d\n",
              prefs.getInt("startDay"),
              prefs.getInt("startMonth"),
              prefs.getInt("startYear"),
              prefs.getInt("jumlahIkan"));
        Serial.printf("Waktu Saat Ini: %02d:%02d:%02d\n", jam, menit, detik);

      }

      if (detik == 0) {
        int pemberianPakan;
        if (jam == 9) {
          pemberianPakan = 1;
        } else if (jam == 17) {
          pemberianPakan = 2;
        } else if (jam == 0 && menit == 5) {
          pemberianPakan = 4;
        }

        if (pemberianPakan != 0) {
          if (internetTerhubung) {
            firebaseSetInt("pemberianPakan", pemberianPakan);
          }
          if (pemberianPakan == 1 || pemberianPakan == 2) {
            int totalPakan = hitungPakan(jumlahIkan);
            beriPakan(totalPakan);
          }
        }
      }
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.println("Menghubungkan ke Wi-Fi...");
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nTerhubung ke Wi-Fi");
    configTime(25200, 0, "0.id.pool.ntp.org");
    internetTerhubung = true;
  } else {
    Serial.println("\nGagal terhubung ke Wi-Fi");
    internetTerhubung = false;
  }
}

bool checkInternetConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi(); // Reconnect jika terputus
  }
  return Ping.ping("www.google.com", 3); // Ping untuk memverifikasi koneksi
}

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

void kosongkanTempatPakan() {
  unsigned long startTime = millis();
  bool pembersihanSelesai = false;

  while (!pembersihanSelesai) {
    float beratSaatIni = scale.get_units();

    if (beratSaatIni > 8) {
      myStepper.step(stepsPerRevolution / 10); // Gerakkan motor stepper
      startTime = millis(); // Reset waktu untuk pengecekan timeout
    } else {
      if (millis() - startTime >= 15000) {
        pembersihanSelesai = true; // Hentikan jika sudah lebih dari 15 detik
      }
    }
    // Pastikan untuk keluar dari loop jika berat sudah sesuai
    if (beratSaatIni <= 8 && (millis() - startTime >= 5000)) {
      // Jika berat di bawah 8 gram dan tidak ada perubahan signifikan selama 1 detik
      pembersihanSelesai = true;
    }
  }

  // Pastikan motor berhenti
  myStepper.step(0); // Pastikan motor stepper tidak menerima langkah lebih lanjut
  // Atur coil motor stepper menjadi low power untuk menghentikan tegangan
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
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
    String startDate = firebaseGetString("startAlat");
  // Hilangkan tanda kutip jika ada
    startDate.replace("\"", ""); // Remove double quotes

    // Format dan cetak
    startDate.replace("\\/", "/");  // Remove escape for '/'
    startDate.replace("\\", "");    // Remove any backslashes (\\)

    Serial.println("=== Data Tanggal dari Firebase ===");
    Serial.print("Start Date: ");
    Serial.println(startDate);

    // Parse tanggal menjadi hari, bulan, tahun
    startDay = startDate.substring(0, 2).toInt();
    startMonth = startDate.substring(3, 5).toInt();
    startYear = startDate.substring(6, 10).toInt();
}

int getJumlahIkanFromFirebase() {
  String jumlahIkanStr = firebaseGetString("jumlahIkan"); // Ambil data dari Firebase
  if (jumlahIkanStr.length() > 0) {
    // Hilangkan karakter tambahan seperti backslash (\) dan tanda kutip ganda (")
    jumlahIkanStr.replace("\\", "");   // Hapus backslash
    jumlahIkanStr.replace("\"", "");  // Hapus tanda kutip ganda

    // Konversi string yang sudah bersih ke integer
    return jumlahIkanStr.toInt();
  }
  return 100; // Jika gagal, kembalikan nilai default 100
}

void setupFirebase() {
  // Firebase Setup
  config.api_key = API_KEY;
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase success");
    signupOK = true;
  } else {
    String firebaseErrorMessage = config.signer.signupError.message.c_str();
    Serial.printf("%s\n", firebaseErrorMessage);
  }
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

// Fungsi untuk membaca jarak menggunakan sensor Ultrasonik
long readUltrasonicDistance() {
  // Mengirimkan pulsa Trigger
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  
  // Menghitung waktu yang diperlukan pulsa untuk kembali ke sensor
  long durasi = pulseIn(ECHO_PIN, HIGH);
  
  // Menghitung jarak berdasarkan waktu durasi
  long jarak = (durasi / 2) * 0.0344;  // Kecepatan suara adalah 343 m/s (0.0344 cm/µs)
  
  return jarak;
}

int hitungKetersediaanPakan(long jarak) {
  if (jarak > 25) {
    jarak = 25;
  }
  if (jarak < 1) {
    jarak = 1;
  }

  // Menghitung ketersediaan pakan berdasarkan jarak
  float ketersediaan = ((25.0 - jarak) / 24.0) * 100;
  Serial.print("Jarak: ");
  Serial.print(jarak);
  Serial.print(" cm, Ketersediaan: ");
  Serial.println(ketersediaan);
  return (int)ketersediaan;
}