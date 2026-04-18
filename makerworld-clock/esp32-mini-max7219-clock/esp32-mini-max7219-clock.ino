// ESP32-C3 Mini LED Clock with MAX7219 & DHT22
// Clean version — no MQTT, no Home Assistant, no OTA
// Just clock + temperature + humidity + auto-brightness + sleep mode

#include <WiFi.h>
#include <time.h>
#include <MD_Parola.h>
#include <SPI.h>
#include <DHT.h>

#include "Font_Data.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DHTTYPE DHT22

#define CLK_PIN   4    // MAX7219 CLK (SCK)
#define DATA_PIN  6    // MAX7219 DATA (MOSI)
#define CS_PIN    7    // MAX7219 CS (SS)
#define LDR_PIN   0    // KY-018 Photoresistor (ADC)
#define DHTPIN    2    // DHT22 DATA
#define LED_PIN   8    // Built-in blue LED on ESP32-C3 Mini

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
DHT dht(DHTPIN, DHTTYPE);

#define SPEED_TIME 75
#define PAUSE_TIME 0
#define MAX_MESSAGE_LENGTH 20

#define SENSOR_UPDATE_INTERVAL 900000UL  // 15 minutes
#define CLOCK_DISPLAY_DURATION 60000UL   // show clock for 60s before cycling

// ===== SLEEP SCHEDULE =====
#define NIGHT_HOUR_START 0
#define NIGHT_MIN_START  0
#define NIGHT_HOUR_END   8
#define NIGHT_MIN_END    0

// ===== WIFI CONFIG =====
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ===== TIMEZONE =====
// Default: Portugal (WET/WEST with automatic DST)
// Change to your timezone: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char* TIMEZONE_STRING = "WET0WEST,M3.5.0/1,M10.5.0";

// ===== BRIGHTNESS =====
const int LDR_DARK_THRESHOLD = 3000;
const int MAX_BRIGHTNESS = 8;
const int MIN_BRIGHTNESS = 1;

// ===== GLOBALS =====
uint16_t h, m, s;
char szTime[9];
char szMesg[MAX_MESSAGE_LENGTH + 1];

float currentTemp;
float currentHum;
uint32_t lastSensorCheck = 0;
bool isSleepMode = false;
uint8_t display = 0;

uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 }; // °C symbol
const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

enum LoaderMode {
  LOADER_DOTS_ONLY,
  LOADER_TEXT_WITH_DOTS,
  LOADER_STATIC_TEXT
};

// ===== TIME =====
void updateTimeVarsOnly() {
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  h = p_tm->tm_hour;
  m = p_tm->tm_min;
  s = p_tm->tm_sec;
}

void showTimeOnDisplay(char *psz, bool f = true) {
  sprintf(psz, "%02d%c%02d", h, (f ? ':' : ' '), m);
}

void showDateOnDisplay(char *psz) {
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  sprintf(psz, "%s %d", dayNames[p_tm->tm_wday], p_tm->tm_mday);
}

// ===== BRIGHTNESS =====
void checkAndAdjustBright() {
  int ldrValue = analogRead(LDR_PIN);
  int brightness = map(ldrValue, 0, LDR_DARK_THRESHOLD, MAX_BRIGHTNESS, MIN_BRIGHTNESS);
  brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
  P.setIntensity(brightness);
}

// ===== SLEEP MODE =====
bool isNightTime() {
  int nowMinutes = h * 60 + m;
  int startMinutes = NIGHT_HOUR_START * 60 + NIGHT_MIN_START;
  int endMinutes = NIGHT_HOUR_END * 60 + NIGHT_MIN_END;

  if (startMinutes <= endMinutes) {
    return nowMinutes >= startMinutes && nowMinutes <= endMinutes;
  }
  return nowMinutes >= startMinutes || nowMinutes <= endMinutes;
}

void handleSleepMode() {
  updateTimeVarsOnly();
  bool nowSleep = isNightTime();
  static bool lastSleepState = false;

  if (nowSleep && !lastSleepState) {
    P.displayShutdown(true);
    P.displaySuspend(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    isSleepMode = true;
  }
  else if (!nowSleep && lastSleepState) {
    connectToWiFi(ssid, password);
    getTimeNTP();
    updateSensorReadings();

    P.displayShutdown(false);
    P.displaySuspend(false);
    P.displayReset(0);

    isSleepMode = false;
    display = 0;
    sprintf(szMesg, "T: %.0f $", isnan(currentTemp) ? 0.0 : currentTemp);
    P.displayZoneText(0, szMesg, PA_CENTER, SPEED_TIME, 0, PA_PRINT, PA_NO_EFFECT);
    P.displayReset(0);
  }

  lastSleepState = nowSleep;
}

// ===== SENSOR =====
void updateSensorReadings() {
  checkAndAdjustBright();

  float temperature = NAN;
  float hum = NAN;

  for (int i = 0; i < 3; i++) {
    temperature = dht.readTemperature();
    if (!isnan(temperature)) break;
    delay(100);
  }

  for (int i = 0; i < 3; i++) {
    hum = dht.readHumidity();
    if (!isnan(hum)) break;
    delay(100);
  }

  currentTemp = !isnan(temperature) ? temperature : NAN;
  currentHum = !isnan(hum) ? hum : NAN;
}

// ===== LOADER ANIMATION =====
void showLoader(LoaderMode mode, const char* text = nullptr, uint16_t durationMs = 3000) {
  const char* dots[] = { ". ", "..", "..." };
  const uint8_t steps = sizeof(dots) / sizeof(dots[0]);
  uint8_t i = 0;
  uint32_t start = millis();

  P.setFont(0, nullptr);
  P.setTextEffect(0, PA_PRINT, PA_NO_EFFECT);
  P.setPause(0, 0);

  if (mode == LOADER_STATIC_TEXT && text != nullptr) {
    P.displayClear();
    P.displayZoneText(0, (char*)text, PA_CENTER, 70, 0, PA_PRINT, PA_NO_EFFECT);
    P.displayAnimate();
    delay(durationMs);
    P.displayClear();
    return;
  }

  while (millis() - start < durationMs) {
    String msg;
    if (mode == LOADER_TEXT_WITH_DOTS && text != nullptr) {
      msg = String(text) + dots[i];
    } else {
      msg = dots[i];
    }
    P.displayClear();
    P.displayZoneText(0, (char*)msg.c_str(), PA_CENTER, 70, 0, PA_PRINT, PA_NO_EFFECT);
    P.displayAnimate();
    delay(500);
    i = (i + 1) % steps;
  }
  P.displayClear();
}

void showLoaderStep(const char* baseText, uint8_t step) {
  const char* dots[] = { ". ", "..", "..." };
  String msg = String(baseText) + dots[step % 3];
  P.displayClear();
  P.displayZoneText(0, (char*)msg.c_str(), PA_CENTER, 70, 0, PA_PRINT, PA_NO_EFFECT);
  P.displayAnimate();
}

// ===== WIFI =====
void connectToWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  const int maxAttempts = 50;
  int attempts = 0;

  showLoader(LOADER_TEXT_WITH_DOTS, "WiFi");

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    showLoaderStep("WIFI", attempts);
    delay(200);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    showLoader(LOADER_STATIC_TEXT, "OK!", 2000);
  } else {
    showLoader(LOADER_STATIC_TEXT, "ERR!", 2000);
    delay(1000);
    showLoader(LOADER_STATIC_TEXT, "reboot", 1000);
    ESP.restart();
  }
}

// ===== NTP =====
void getTimeNTP() {
  showLoader(LOADER_TEXT_WITH_DOTS, "NTP", 1000);

  configTzTime(TIMEZONE_STRING, "pool.ntp.org", "time.nist.gov", "europe.pool.ntp.org");

  int retry = 0;
  time_t now = time(nullptr);
  while ((now < 1000000000) && retry < 20) {
    showLoaderStep("NTP", retry);
    delay(500);
    now = time(nullptr);
    retry++;
  }

  if (now < 1000000000UL) {
    showLoader(LOADER_STATIC_TEXT, "ERR!", 2000);
  } else {
    showLoader(LOADER_STATIC_TEXT, "OK!", 2000);
  }

  // Disconnect WiFi after NTP sync to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ===== SETUP =====
void setup(void) {
  Serial.begin(115200);
  delay(1500);

  P.begin(3);
  P.setZone(0, MAX_DEVICES - 4, MAX_DEVICES - 1);
  P.setInvert(false);
  P.addChar('$', degC);

  // Turn on built-in LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // active LOW on ESP32-C3 Mini

  showLoader(LOADER_DOTS_ONLY, nullptr, 3000);

  connectToWiFi(ssid, password);
  getTimeNTP();

  dht.begin();
  delay(2000);
  updateSensorReadings();
  updateTimeVarsOnly();

  P.setFont(0, nullptr);
  P.setZone(1, MAX_DEVICES - 4, MAX_DEVICES - 1);
  P.displayZoneText(1, szTime, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(0, szMesg, PA_CENTER, SPEED_TIME, 0, PA_PRINT, PA_NO_EFFECT);

  showTimeOnDisplay(szTime, true);
  lastSensorCheck = millis();
}

// ===== LOOP =====
void loop(void) {
  handleSleepMode();
  if (isSleepMode) return;

  uint32_t currentMillis = millis();
  static uint32_t lastTime = 0;
  static uint32_t phaseStartTime = 0;
  static bool flasher = false;
  static String lastSzTime = "";

  P.displayAnimate();

  // Sensor update
  if (currentMillis - lastSensorCheck >= SENSOR_UPDATE_INTERVAL) {
    lastSensorCheck = currentMillis;
    updateSensorReadings();
  }

  if (P.getZoneStatus(0)) {
    switch (display) {
      case 0: {  // Clock
        P.setPause(0, 0);
        P.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        P.setFont(0, numeric7Seg);

        if (currentMillis - lastTime >= 1000) {
          lastTime = currentMillis;
          updateTimeVarsOnly();
          showTimeOnDisplay(szTime, flasher);
          flasher = !flasher;

          String nowTime = String(szTime);
          if (nowTime != lastSzTime) {
            P.setTextBuffer(1, szTime);
            P.displayReset(1);
            lastSzTime = nowTime;
          }
        }

        if (phaseStartTime == 0) phaseStartTime = currentMillis;

        if (currentMillis - phaseStartTime >= CLOCK_DISPLAY_DURATION) {
          display++;
          P.setFont(0, nullptr);
          P.setTextEffect(0, PA_MESH, PA_BLINDS);
          P.setPause(0, 2000);
          phaseStartTime = 0;
        }
        break;
      }

      case 1: {  // Date
        showDateOnDisplay(szMesg);
        P.setPause(0, 2000);
        P.setTextEffect(0, PA_SCROLL_UP, PA_SCROLL_DOWN);
        P.displayReset(0);
        display++;
        break;
      }

      case 2: {  // Temperature
        if (!isnan(currentTemp)) {
          sprintf(szMesg, "T: %.0f $", currentTemp);
        } else {
          strcpy(szMesg, "T: -- $");
        }
        P.setPause(0, 2000);
        P.setTextEffect(0, PA_MESH, PA_BLINDS);
        P.displayReset(0);
        display++;
        break;
      }

      case 3: {  // Humidity
        if (!isnan(currentHum)) {
          sprintf(szMesg, "H: %.0f %%", currentHum);
        } else {
          strcpy(szMesg, "H: -- %%");
        }
        P.setPause(0, 2000);
        P.setTextEffect(0, PA_OPENING, PA_GROW_DOWN);
        P.displayReset(0);
        display = 0;
        break;
      }

      default:
        display = 0;
        break;
    }
  }
}
