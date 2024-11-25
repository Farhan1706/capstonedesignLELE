#include <ESP32Servo.h>
#include "HX711.h"
#include <Stepper.h>
#include <time.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "RTClib.h"
#include <WiFiManager.h>
#include <Preferences.h>  // Tambahkan library Preferences

// FIREBASE SETUP
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
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
int stokPakan = 0;
int jumlahIkan = 100;
int startYear=0, startMonth=0, startDay=0;
bool sinkronisasiSiap = false;


void setup() {
  Serial.begin(115200);

  // Inisialisasi Preferences untuk menyimpan tanggal dan jumlah ikan
  prefs.begin("startDate", false);

  // Cek apakah tanggal sudah ada di memori
  if (prefs.isKey("startDay") && prefs.isKey("startMonth") && prefs.isKey("startYear")) {
    // Ambil tanggal dari NVS jika tersedia
    startDay = prefs.getInt("startDay");
    startMonth = prefs.getInt("startMonth");
    startYear = prefs.getInt("startYear");
    jumlahIkan = prefs.getInt("jumlahIkan");  // Ambil jumlah ikan dari Preferences
    Serial.println("Loaded start date and jumlah ikan from NVS:");
    Serial.print("Start Day (from Preferences): ");
    Serial.println(startDay);
    Serial.print("Start Month (from Preferences): ");
    Serial.println(startMonth);
    Serial.print("Start Year (from Preferences): ");
    Serial.println(startYear);
    Serial.print("Jumlah Ikan (from Preferences): ");
    Serial.println(jumlahIkan);
  } else {
    Serial.println("No start date found in NVS.");
  }

  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Koneksi Wi-Fi
  connectWiFi();
  sinkronisasiWaktu();

  // Firebase setup
  setupFirebase();
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // RTC setup
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi!");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC kehilangan daya. Mengatur ulang waktu ke waktu kompilasi.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Ambil data awal dari Firebase
  // Mendapatkan jumlah ikan dari Firebase dan menyimpannya di Preferences
  String jumlahIkanString;
  jumlahIkan = getJumlahIkanFromFirebase();
  Serial.println(jumlahIkan);

  // Jika gagal mendapatkan jumlah ikan, gunakan nilai default
  if (jumlahIkan == 0) {
      Serial.println("Gagal mengambil jumlah ikan dari Firebase. Menggunakan nilai default.");
      jumlahIkan = 100; // Default fallback value
  }

  // Menyimpan jumlah ikan ke Preferences
  prefs.putInt("jumlahIkan", jumlahIkan);
  Serial.println("Jumlah ikan yang digunakan:");
  Serial.println(jumlahIkan);


  // Servo
  servo1.attach(servoPin);
  servo1.write(0);

  // Load Cell
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  // Stepper
  myStepper.setSpeed(10);


  String startAlat = firebaseGetString("startAlat");
  parseStartDate(startAlat);
}

void loop() {
  // Mengukur jarak menggunakan sensor ultrasonik
  long jarak = readUltrasonicDistance();
  
  // Menghitung ketersediaan pakan berdasarkan jarak
  int ketersediaanPakan = hitungKetersediaanPakan(jarak);
  
  // Menampilkan hasil pengukuran jarak dan ketersediaan pakan
  Serial.print("Jarak dari sensor ultrasonik: ");
  Serial.print(jarak);
  Serial.print(" cm, Ketersediaan Pakan: ");
  Serial.print(ketersediaanPakan);
  Serial.println("%");
  firebaseSetInt("stokPakan", ketersediaanPakan);

  // Get string from Firebase
  String startDate = firebaseGetString("startAlat"); // Firebase returns in format "20\\/11\\/2024"
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
    Serial.print("Start Day (from Firebase): ");
    Serial.println(startDay);
    Serial.print("Start Month (from Firebase): ");
    Serial.println(startMonth);
    Serial.print("Start Year (from Firebase): ");
    Serial.println(startYear);

    // Simpan tanggal ke NVS
    prefs.putInt("startDay", startDay);
    prefs.putInt("startMonth", startMonth);
    prefs.putInt("startYear", startYear);
    prefs.putInt("jumlahIkan", jumlahIkan);
    Serial.println("Start date saved to NVS.");

    // Calculate the difference in days between the RTC date and the Firebase start date
    DateTime now = rtc.now();
    
    // Calculate hari berjalan (difference in days)
    DateTime startDateTime(startYear, startMonth, startDay); 
    int hariBerjalan = (now - startDateTime).days();  // Hitung selisih hari
    Serial.print("Hari Berjalan: ");
    Serial.println(hariBerjalan);

    // Print data from both Firebase and Preferences
    Serial.println("\n----- Data from Firebase and Preferences -----");
    Serial.print("Start Day (from Firebase): ");
    Serial.println(startDay);
    Serial.print("Start Month (from Firebase): ");
    Serial.println(startMonth);
    Serial.print("Start Year (from Firebase): ");
    Serial.println(startYear);
    Serial.print("Jumlah Ikan (from Firebase): ");
    Serial.println(jumlahIkan);

    Serial.print("Start Day (from Preferences): ");
    Serial.println(prefs.getInt("startDay"));
    Serial.print("Start Month (from Preferences): ");
    Serial.println(prefs.getInt("startMonth"));
    Serial.print("Start Year (from Preferences): ");
    Serial.println(prefs.getInt("startYear"));
    Serial.print("Jumlah Ikan (from Preferences): ");
    Serial.println(prefs.getInt("jumlahIkan"));
  } else {
    Serial.println("Failed to get data from Firebase.");
  }

  DateTime now = rtc.now();
  int hariBerjalan = (now - DateTime(startYear, startMonth, startDay)).days();
  int jam = now.hour();
  int menit = now.minute();
  int detik = now.second();

  // Pemberian Pakan Otomatis
  if (detik == 0) {
    int pemberianPakan = 4;

    // Tentukan nilai pemberianPakan berdasarkan waktu
    if (jam == 9) {
        pemberianPakan = 1;
    } else if (jam == 15) {
        pemberianPakan = 2;
    } else if (jam == 21) {
        pemberianPakan = 3;
    } else if (jam == 0 && menit == 5) {
        pemberianPakan = 4;
    }

    // Kirim nilai pemberianPakan ke Firebase
    if (pemberianPakan != 0) {
        firebaseSetInt("pemberianPakan", pemberianPakan);

        // Hanya memberikan pakan untuk nilai 1, 2, atau 3
        if (pemberianPakan == 1 || pemberianPakan == 2 || pemberianPakan == 3) {
            int totalPakan = hitungPakan(jumlahIkan);
            beriPakan(totalPakan);
        }
    }
  }
  if (jam == 23 && menit == 30 && !WiFi.isConnected()) {
    sinkronisasiSiap = true;
    connectWiFi();
  }

  // Sinkronisasi pada RTC tepat pukul 12:00:00
  if (jam == 0 && menit == 0 && now.second() == 0 && sinkronisasiSiap && WiFi.isConnected()) {
    sinkronisasiWaktu();
    sinkronisasiSiap = false; // Set ulang penanda setelah sinkronisasi
  }

  // Pemberian Pakan Manual
  String fnberiPakan = firebaseGetString("beriPakan");

  if (fnberiPakan == "1") {
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

//Connect baru
void connectWiFi() {
  // Membuat objek WiFiManager
  WiFiManager wifiManager;

  // Jika sudah terhubung ke Wi-Fi, lewati proses koneksi ulang
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Sudah terhubung ke Wi-Fi");
    return;
  }

  // Coba hubungkan ke Wi-Fi menggunakan WiFiManager
  Serial.println("Menghubungkan ke Wi-Fi menggunakan WiFiManager...");
  if (!wifiManager.autoConnect("AlatPakanLele_AP")) {  // Nama AP mode jika Wi-Fi gagal
    Serial.println("Gagal terhubung ke Wi-Fi. Mode AP diaktifkan.");
    delay(1000);
    return;  // Keluar dari fungsi jika koneksi gagal
  }

  // Jika berhasil terhubung, tampilkan alamat IP
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Terhubung ke Wi-Fi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Sinkronkan waktu menggunakan NTP
    Serial.println("Menyinkronkan waktu dengan NTP...");
    configTime(25200, 0, "0.id.pool.ntp.org");  // Atur offset waktu (25200 detik untuk WIB)
    struct tm timeInfo;
    if (getLocalTime(&timeInfo)) {
      Serial.println("Waktu berhasil disinkronkan!");
      Serial.printf("Waktu lokal: %s\n", asctime(&timeInfo));
    } else {
      Serial.println("Gagal menyinkronkan waktu.");
    }
  } else {
    Serial.println("Koneksi Wi-Fi gagal. Tetap dalam mode AP.");
  }
}

// Kode Wifi Lama
// void connectWiFi() {
//   WiFiManager wifiManager;

//   // Nama Access Point WiFi Manager
//   String apName = "PakanLele_AP";
  
//   // Konfigurasi ulang jika tidak ada koneksi Wi-Fi
//   if (!wifiManager.autoConnect(apName.c_str())) {
//     Serial.println("Gagal terhubung ke Wi-Fi. Mode Access Point diaktifkan.");
//     delay(1000);

//     // WiFiManager akan tetap berjalan dalam mode AP sampai pengguna memasukkan kredensial Wi-Fi baru
//     while (WiFi.status() != WL_CONNECTED) {
//       delay(500);
//       Serial.print(".");
//     }
//   } else {
//     Serial.println("Terhubung ke Wi-Fi!");
//     // configTime(25200, 0, "0.id.pool.ntp.org");
//   }

//   // Cek status koneksi
//   if (WiFi.status() == WL_CONNECTED) {
//     Serial.println("Koneksi Wi-Fi berhasil!");
//     Serial.print("IP Address: ");
//     Serial.println(WiFi.localIP());
//   } else {
//     Serial.println("Masih dalam mode Access Point.");
//   }
// }

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

int getJumlahIkanFromFirebase() {
  String jumlahIkanStr = firebaseGetString("startAlat"); // Ambil data dari Firebase
  if (jumlahIkanStr.length() > 0) {
    // Hilangkan karakter tambahan seperti backslash (\) dan tanda kutip ganda (")
    jumlahIkanStr.replace("\\", "");   // Hapus backslash
    jumlahIkanStr.replace("\"", "");  // Hapus tanda kutip ganda

    // Konversi string yang sudah bersih ke integer
    return jumlahIkanStr.toInt();
  }
  return 100; // Jika gagal, kembalikan nilai default 100
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