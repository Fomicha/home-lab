// Header file includes
#define DEBUG
#include <WiFi.h>
#include <time.h>
#include <MD_Parola.h>
#include <SPI.h>
#include <DHT.h>

#include "Font_Data.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define DHTTYPE DHT11

#define CLK_PIN 18 // or SCK
#define DATA_PIN 23 // or MOSI
#define CS_PIN 15 // or SS
#define LDR_PIN 34 // Photoresistor
#define DHTPIN 4 // D4 = GPIO4

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
DHT dht(DHTPIN, DHTTYPE);

#define SPEED_TIME 75
#define PAUSE_TIME 0
#define MAX_MESSAGE_LENGTH 20

#ifdef DEBUG
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#define SENSOR_UPDATE_INTERVAL 900000UL // 1800000UL - 30хв .. 900000UL - 15хв
#define CLOCK_DISPLAY_DURATION 60000UL

#define NIGHT_HOUR_START 0
#define NIGHT_MIN_START 0
#define NIGHT_HOUR_END 8
#define NIGHT_MIN_END 0

/**********  User Config Setting   ******************************/
#include "../../config/secrets.h"
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

// calculate your timezone in seconds, +1 hour = 3600 seconds
const int TIMEZONE_IN_SECONDS = 3600; // UTC+1 для Португалії (літній час)
/***************************************************************/

int dst = 0;
uint16_t h, m, s;

// Global variables
char szTime[9]; // mm:ss\0
char szMesg[MAX_MESSAGE_LENGTH + 1];

float currentTemp;
float currentHum;
uint32_t lastHalfHourCheck = 0;
bool isSleepMode = false;

const int LDR_DARK_THRESHOLD = 3000; // Поріг, після якого вважається що темно
const int MAX_BRIGHTNESS = 1;  // Максимальна яскравість (економія батареї)
const int MIN_BRIGHTNESS = 1;  // Мінімальна яскравість (економія батареї)

uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 }; // символ °C

enum LoaderMode {
  LOADER_DOTS_ONLY,       // . .. ...
  LOADER_TEXT_WITH_DOTS,  // WIFI. WIFI.. WIFI...
  LOADER_STATIC_TEXT      // OK / ERR / REBOOT
};

void updateTimeVarsOnly() {
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  h = p_tm->tm_hour;
  m = p_tm->tm_min;
  s = p_tm->tm_sec;
}

void showTimeOnDisplay(char *psz, bool f = true) {
  sprintf(psz, "%02d%c%02d", h, (f ? ':' : ' '), m);
  DEBUG_PRINTLN(psz);
}

void checkAndAdjustBright() {
  int ldrValue = analogRead(LDR_PIN);  // ~0 (світло) до ~4095 (темно)

  DEBUG_PRINT("LDR: ");
  DEBUG_PRINT(ldrValue);
  DEBUG_PRINTLN();

  int brightness = map(ldrValue, 0, LDR_DARK_THRESHOLD, MAX_BRIGHTNESS, MIN_BRIGHTNESS);
  brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

  DEBUG_PRINT("Intensity: ");
  DEBUG_PRINT(brightness);
  DEBUG_PRINTLN();

  P.setIntensity(brightness);
}

bool isNightTime() {
  int nowMinutes = h * 60 + m;
  int startMinutes = NIGHT_HOUR_START * 60 + NIGHT_MIN_START;
  int endMinutes = NIGHT_HOUR_END * 60 + NIGHT_MIN_END;

  bool result;
  if (startMinutes <= endMinutes) {
    result = nowMinutes >= startMinutes && nowMinutes <= endMinutes;
  } else {
    result = nowMinutes >= startMinutes || nowMinutes <= endMinutes;
  }

  // DEBUG_PRINTF("[NIGHT CHECK] now: %02d:%02d → %s\n", h, m, result ? "НІЧ" : "ДЕНЬ");
  return result;
}

void handleSleepMode() {
  updateTimeVarsOnly();
  bool nowSleep = isNightTime();

  static bool lastSleepState = false;

  if (nowSleep && !lastSleepState) {
    DEBUG_PRINTLN("[SLEEP] Вимикаємо все");

    P.displayShutdown(true);
    P.displaySuspend(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    DEBUG_PRINTLN("[SLEEP] WiFi вимкнено");
    isSleepMode = true;
  }
  else if (!nowSleep && lastSleepState) {
    DEBUG_PRINTLN("[SLEEP] Прокидаємось");

    connectToWiFi(ssid, password); 
    DEBUG_PRINTLN("\n[SLEEP] WiFi знову підключено");

    getTimeNTP();
    updateSensorReadings();
    DEBUG_PRINTF("[WAKE] TEMP=%.1f, HUM=%.1f\n", currentTemp, currentHum);

    P.displayShutdown(false);
    P.displaySuspend(false);
    P.displayReset(0);

    isSleepMode = false;
    display = 0;
    sprintf(szMesg, "T: %.0f $", isnan(currentTemp) ? 0.0 : currentTemp);
  }

  lastSleepState = nowSleep;
}

void updateSensorReadings() {
  checkAndAdjustBright();

  // Спроби зчитування температури
  float temperature = NAN;
  float hum = NAN;
  
  for (int i = 0; i < 3; i++) { // 3 спроби
    temperature = dht.readTemperature();
    if (!isnan(temperature)) break;
    delay(100);
  }
  
  for (int i = 0; i < 3; i++) { // 3 спроби
    hum = dht.readHumidity();
    if (!isnan(hum)) break;
    delay(100);
  }

  if (!isnan(temperature)) {
    currentTemp = temperature;
    DEBUG_PRINTF("[TEMP] Оновлено: %.1f °C\n", currentTemp);
  } else {
    currentTemp = NAN;
    DEBUG_PRINTLN("[TEMP] Помилка зчитування після 3 спроб");
  }

  if (!isnan(hum)) {
    currentHum = hum;
    DEBUG_PRINTF("[HUM] Оновлено: %.1f %%\n", currentHum);
  } else {
    currentHum = NAN;
    DEBUG_PRINTLN("[HUM] Помилка зчитування після 3 спроб");
  }
}

void showLoader(LoaderMode mode, const char* text = nullptr, uint16_t durationMs = 3000) {
  const char* dots[] = { ". ", "..", "..." };
  const uint8_t steps = sizeof(dots) / sizeof(dots[0]);
  uint8_t i = 0;
  uint32_t start = millis();

  P.setFont(0, nullptr); // маленький системний шрифт
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
    } else if (mode == LOADER_DOTS_ONLY) {
      msg = dots[i];
    } else {
      msg = "???"; // fallback
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

void getTimeNTP() {
  showLoader(LOADER_TEXT_WITH_DOTS, "NTP", 1000);

  configTime(TIMEZONE_IN_SECONDS, dst, "pool.ntp.org", "time.nist.gov", "europe.pool.ntp.org");

  DEBUG_PRINTLN("[NTP] Очікуємо синхронізацію часу...");

  int retry = 0;
  const int maxRetries = 20;

  time_t now = time(nullptr);
  while ((now < 1000000000) && retry < maxRetries) {  // час менше 2001 року
    showLoaderStep("NTP", retry);
    delay(500);
    DEBUG_PRINT(".");
    now = time(nullptr);
    retry++;
  }

  DEBUG_PRINTLN();

  if (now < 1000000000UL) {
    DEBUG_PRINTLN("[NTP] Помилка синхронізації часу");
    showLoader(LOADER_STATIC_TEXT, "ERR!", 2000);
  } else {
    struct tm* timeinfo = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", timeinfo);

    DEBUG_PRINT("[NTP] Час синхронізовано: ");
    DEBUG_PRINTLN(buf);

    showLoader(LOADER_STATIC_TEXT, "OK!", 2000);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

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
    Serial.println("\n[WiFi] Підключено");
    DEBUG_PRINTLN("[WiFi] IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
    showLoader(LOADER_STATIC_TEXT, "OK!", 2000);
  } else {
    DEBUG_PRINTLN("\n[WiFi] Помилка підключення!");
    showLoader(LOADER_STATIC_TEXT, "ERR!", 2000);
    delay(1000);
    showLoader(LOADER_STATIC_TEXT, "reboot", 1000);
    ESP.restart();
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(100);

  P.begin(3);
  P.setZone(0,  MAX_DEVICES-4, MAX_DEVICES-1);
  P.setInvert(false);
  P.addChar('$', degC);

  showLoader(LOADER_DOTS_ONLY, nullptr, 3000);

  connectToWiFi(ssid, password); 
  getTimeNTP();
  dht.begin();
  updateSensorReadings();
  updateTimeVarsOnly();

  P.setFont(0, nullptr);
  P.setZone(1, MAX_DEVICES-4, MAX_DEVICES-1);
  P.displayZoneText(1, szTime, PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(0, szMesg, PA_CENTER, SPEED_TIME, 0, PA_PRINT , PA_NO_EFFECT);

  showTimeOnDisplay(szTime, true); // Показуємо час на екран
  DEBUG_PRINT("[BOOT TIME] ");
  DEBUG_PRINTLN(szTime);

  lastHalfHourCheck = millis();
}

void loop(void) {
  handleSleepMode();

  if (isSleepMode) return;

  uint32_t currentMillis = millis();
  static uint32_t lastTime = 0;
  static uint32_t phaseStartTime = 0;
  static uint8_t display = 0;
  static bool flasher = false;
  static String lastSzTime = "";

  P.displayAnimate();

  // Оновлення кожні 30 хвилин
  if (currentMillis - lastHalfHourCheck >= SENSOR_UPDATE_INTERVAL) {
    lastHalfHourCheck = currentMillis;
    updateSensorReadings();
  }

  if (P.getZoneStatus(0)) {
    switch (display) {
      case 0: {  // Показуємо час
        P.setPause(0, 0);
        P.setTextEffect(0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        P.setFont(0, numeric7Seg);

        // ⏱ оновлення годинника рівно раз на секунду
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

      case 1: {
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

      case 2: {
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

      default: {
        display = 0;
        break;
      }
    }
  }
}
