#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif

#include <Firebase_ESP_Client.h>
#include <time.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Firebase Helpers
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Wi-Fi credentials
#define WIFI_SSID "YUVARAJSWIFI"
#define WIFI_PASSWORD "12345678"

// Firebase credentials
#define API_KEY "AIzaSyCbFaD9IhsXATJw2pCCJ_93iqx9FGFfRvQ"
#define DATABASE_URL "https://upkisaan-6246b-default-rtdb.firebaseio.com/"

// Firebase objects
FirebaseData fbdo;
FirebaseData motorStatusFBDO;
FirebaseAuth auth;
FirebaseConfig config;

// Sensor pin definitions
#define SOIL_MOISTURE_30 36
#define SOIL_MOISTURE_60 39
#define RAIN_SENSOR      34
#define PIR_SENSOR        5
#define WIND_SPEED        4
#define DHTPIN           32
#define DHTTYPE          DHT11
#define MOTOR_PUMP_PIN   27

// I2C LCD: 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2); // Use 0x27 or 0x3F depending on your module

// DHT Sensor
DHT dht(DHTPIN, DHTTYPE);

// Time and loop interval
unsigned long sendDataPrevMillis = 0;
unsigned long interval = 10000;
bool signupOK = false;
struct tm tmstruct;

void printLocalTime() {
  char buffer[30];
  sprintf(buffer, "%02d/%02d/%02d %02d:%02d:%02d",
          tmstruct.tm_mday, tmstruct.tm_mon + 1, tmstruct.tm_year % 100,
          tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
  Serial.print("Local Time (IST): ");
  Serial.println(String(buffer));
}

void setup() {
  Serial.begin(115200);

  // LCD Init
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  // Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected");

  // Firebase config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Signup OK");
    signupOK = true;
  } else {
    Serial.printf("Firebase Signup Failed: %s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // NTP time
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  delay(2000);
  if (getLocalTime(&tmstruct, 5000)) {
    printLocalTime();
  } else {
    Serial.println("Failed to get time from NTP");
  }

  // Sensor Init
  pinMode(SOIL_MOISTURE_30, INPUT);
  pinMode(SOIL_MOISTURE_60, INPUT);
  pinMode(RAIN_SENSOR, INPUT);
  pinMode(PIR_SENSOR, INPUT);
  pinMode(WIND_SPEED, INPUT);
  pinMode(MOTOR_PUMP_PIN, OUTPUT);
  digitalWrite(MOTOR_PUMP_PIN, LOW);
  dht.begin();
}

void loop() {
  static int screenIndex = 0;
  static unsigned long lastSwitchTime = 0;
  const unsigned long screenInterval = 3000; // switch every 3 seconds

  // Read sensors
  int soilMoisture30 = analogRead(SOIL_MOISTURE_30);
  int soilMoisture60 = analogRead(SOIL_MOISTURE_60);
  int rainStatus = digitalRead(RAIN_SENSOR);
  int motionDetected = digitalRead(PIR_SENSOR);
  int windSpeed = analogRead(WIND_SPEED);
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature)) temperature = 0.0;
  if (isnan(humidity)) humidity = 0.0;

  // Firebase read for motor status
  int motorStatus = 0;
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.getInt(&motorStatusFBDO, "gsheetdata/motor")) {
      motorStatus = motorStatusFBDO.intData();
      digitalWrite(MOTOR_PUMP_PIN, motorStatus == 1 ? HIGH : LOW);
    }
  }

  // === TOGGLE LCD DISPLAY ===
  if (millis() - lastSwitchTime > screenInterval) {
    lastSwitchTime = millis();
    lcd.clear();

    switch (screenIndex) {
      case 0:
        lcd.setCursor(0, 0);
        lcd.print("Temp:");
        lcd.print(temperature, 1);
        lcd.print("C");
        lcd.setCursor(0, 1);
        lcd.print("Hum :");
        lcd.print(humidity, 0);
        lcd.print("%");
        break;
      case 1:
        lcd.setCursor(0, 0);
        lcd.print("Soil30:");
        lcd.print(soilMoisture30 / 10);
        lcd.setCursor(0, 1);
        lcd.print("Soil60:");
        lcd.print(soilMoisture60 / 10);
        break;
      case 2:
        lcd.setCursor(0, 0);
        lcd.print("Rain:");
        lcd.print(rainStatus == 1 ? "NO " : "YES");
        lcd.setCursor(0, 1);
        lcd.print("Motion:");
        lcd.print(motionDetected ? "YES" : "NO ");
        break;
      case 3:
        lcd.setCursor(0, 0);
        lcd.print("Wind:");
        lcd.print(windSpeed);
        lcd.setCursor(0, 1);
        lcd.print("Motor:");
        lcd.print(motorStatus == 1 ? "ON " : "OFF");
        break;
    }

    screenIndex = (screenIndex + 1) % 4; // Loop through 4 screens
  }

  // Print time to Serial
  if (millis() % 5000 == 0) {
    if (getLocalTime(&tmstruct, 5000)) {
      printLocalTime();
    }
  }

  // Send data to Firebase
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > interval || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    time_t now;
    time(&now);
    String dataID = "DATA_" + String(now);

    FirebaseJson json;
    json.set("soilMoisture30", soilMoisture30);
    json.set("soilMoisture60", soilMoisture60);
    json.set("rainStatus", rainStatus);
    json.set("motionDetected", motionDetected);
    json.set("windSpeed", windSpeed);
    json.set("temperature", temperature);
    json.set("humidity", humidity);
    json.set("timestamp", now);
    json.set("dataID", dataID);

    if (Firebase.RTDB.pushJSON(&fbdo, "sensorData", &json)) {
      Serial.println("DATA PUSHED");
    } else {
      Serial.println("FAILED: " + fbdo.errorReason());
    }
  }
}
