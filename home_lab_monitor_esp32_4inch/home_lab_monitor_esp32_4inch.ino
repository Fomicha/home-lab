#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <ArduinoOTA.h>

// ===== CONFIG =====
#include "../config/secrets.h"
#define OWM_CITY        "Porto"
#define OWM_COUNTRY     "PT"
#define UPDATE_INTERVAL 5000
#define WEATHER_INTERVAL 600000  // 10 minutes for weather
#define WIFI_RETRY_INTERVAL 15000UL
#define WIFI_MAX_RETRY_INTERVAL 120000UL
#define WIFI_CONNECT_ATTEMPTS 20
#define NET_TIMEOUT_MS 3000
#define WEATHER_TIMEOUT_MS 5000
#define NTP_RESYNC_INTERVAL 21600000UL  // 6 hours
#define SLEEP_HOUR      23
#define WAKE_HOUR       9
#define TIMEZONE_STRING "WET0WEST,M3.5.0/1,M10.5.0"

// ===== LAYOUT =====
#define SCR_W       480
#define SCR_H       320
#define CARD_GAP    4       // gap between cards and screen edges
#define PAD         6       // internal padding within cards

// Grid dimensions
#define LEFT_W      152     // left column card width
#define PROX_H      210     // proxmox card height

// Proxmox card
#define PX_X  CARD_GAP
#define PX_Y  CARD_GAP
#define PX_W  LEFT_W
#define PX_H  PROX_H

// Weather card
#define WX_X  CARD_GAP
#define WX_Y  (PX_Y + PX_H + CARD_GAP)
#define WX_W  LEFT_W
#define WX_H  (SCR_H - WX_Y - CARD_GAP)

// Room sensors column start
#define RS_X  (PX_X + PX_W + CARD_GAP)
#define RS_Y  CARD_GAP

// Room sensor card dimensions
#define CARD_W      130
#define CARD_PAD    6
#define SYS_X       (RS_X + CARD_W + CARD_PAD * 2 + CARD_GAP)
#define SYS_Y       CARD_GAP
#define SYS_W       (SCR_W - SYS_X - CARD_GAP)
#define SYS_H       (SCR_H - SYS_Y - CARD_GAP)

TFT_eSPI tft = TFT_eSPI();

// ===== COLORS =====
#define BG_COLOR      0x0000
#define TEXT_COLOR    0xFFFF
#define LABEL_COLOR   0x7BEF
#define DIVIDER_COLOR 0x2945
#define CPU_COLOR     0xF800
#define RAM_COLOR     0x07E0
#define TEMP_COLOR    0xFD20
#define USB_COLOR     0x001F
#define OK_COLOR      0x07E0
#define FAIL_COLOR    0xF800
#define TITLE_COLOR   0x051F
#define CARD_COLOR    0x1082
#define CYAN_COLOR    0x07FF
#define PINK_COLOR    0xF81F
#define WEATHER_COLOR 0x02D6

// ===== PROXMOX VARIABLES =====
unsigned long lastUpdate = 0;
float cpuUsage = 0, ramUsage = 0, temperature = 0;
float usbStorageUsed = 0, usbStorageTotal = 0;
float usbPhotosUsed = 0, usbPhotosTotal = 0;
unsigned long uptime = 0;
bool firstRun = true;
bool statusPihole = false, statusImmich = false;
bool statusNextcloud = false, statusVaultwarden = false;
unsigned long lastWeatherUpdate = 0;
bool isSleeping = false;
unsigned long lastWiFiRetry = 0;
unsigned long wifiRetryInterval = WIFI_RETRY_INTERVAL;
uint8_t wifiFailCount = 0;
unsigned long lastMetricsSuccess = 0;
unsigned long lastHASuccess = 0;
unsigned long lastWeatherSuccess = 0;
unsigned long lastTimeSync = 0;
bool hasWeatherData = false;
bool otaReady = false;

// ===== HA VARIABLES =====
float livingTemp = 0, livingHum = 0, livingIAQ = 0;
float livingCO2 = 0, livingPressure = 0;
float clockTemp = 0, clockHum = 0;
float babyTemp = 0, babyHum = 0;

// ===== WEATHER VARIABLES =====
float weatherTemp = 0, weatherFeelsLike = 0, weatherHum = 0;
float weatherWindSpeed = 0;
char weatherDesc[32] = "";
char weatherMain[16] = "";

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] STA connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] got IP: %s, RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WiFi] disconnected, reason=%d\n", info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

// ===== HELPERS =====
void drawCentered(const char* text, int y) {
  int16_t tw = strlen(text) * 6 * tft.textsize;
  tft.setCursor((SCR_W - tw) / 2, y);
  tft.print(text);
}

void ensureOTAReady() {
  if (otaReady || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname("homelab-monitor");
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Update started");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update finished");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %u\n", error);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println("[OTA] Ready (port 3232)");
}

inline void otaYield() {
  if (otaReady && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
}

// ===== WIFI =====
bool connectWiFi(bool showSplash = true) {
  if (WiFi.status() == WL_CONNECTED) {
    wifiFailCount = 0;
    wifiRetryInterval = WIFI_RETRY_INTERVAL;
    return true;
  }

  if (showSplash) {
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
    drawCentered("Connecting WiFi...", SCR_H / 2 - 10);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_CONNECT_ATTEMPTS) {
    delay(500); attempts++;
  }

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    wifiFailCount = 0;
    wifiRetryInterval = WIFI_RETRY_INTERVAL;
    Serial.printf("[WiFi] connected: %s, RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    if (wifiFailCount < 6) wifiFailCount++;
    wifiRetryInterval = WIFI_RETRY_INTERVAL << wifiFailCount;
    if (wifiRetryInterval > WIFI_MAX_RETRY_INTERVAL) wifiRetryInterval = WIFI_MAX_RETRY_INTERVAL;
    Serial.printf("[WiFi] failed, status=%d, next retry in %lus\n", WiFi.status(), wifiRetryInterval / 1000);
  }

  if (showSplash) {
    tft.fillScreen(BG_COLOR);
    drawCentered(connected ? "WiFi Connected!" : "WiFi Failed", SCR_H / 2 - 10);
    delay(connected ? 800 : 1200);
  }

  return connected;
}

// ===== NETDATA =====
bool getNetdataValue(const char* chart, int valueIndex, float& value) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "http://" + String(NETDATA_HOST) + ":" + String(NETDATA_PORT) +
               "/api/v1/data?chart=" + String(chart) + "&after=-1&format=json";
  http.begin(url); http.setTimeout(NET_TIMEOUT_MS);

  bool ok = false;
  if (http.GET() == 200) {
    DynamicJsonDocument doc(8192);
    if (!deserializeJson(doc, http.getString())) {
      JsonArray data = doc["data"];
      if (data.size() > 0) {
        JsonArray values = data[0];
        if (values.size() > valueIndex) {
          value = values[valueIndex].as<float>();
          ok = true;
        }
      }
    }
  }
  http.end();
  return ok;
}

// ===== HA API =====
bool getHAState(const char* entity_id, float& value) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "http://" + String(HA_HOST) + ":" + String(HA_PORT) +
               "/api/states/" + String(entity_id);
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(NET_TIMEOUT_MS);

  bool ok = false;
  if (http.GET() == 200) {
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, http.getString())) {
      JsonVariantConst state = doc["state"];
      if (state.is<const char*>()) {
        const char* s = state.as<const char*>();
        char* endPtr = nullptr;
        float parsed = strtof(s, &endPtr);
        if (endPtr != s) {
          value = parsed;
          ok = true;
        }
      } else if (state.is<float>() || state.is<int>() || state.is<long>() || state.is<double>()) {
        value = state.as<float>();
        ok = true;
      }
    }
  }
  http.end();
  return ok;
}

// ===== WEATHER API =====
bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" +
               String(OWM_CITY) + "," + String(OWM_COUNTRY) +
               "&appid=" + String(OWM_API_KEY) +
               "&units=metric";
  http.begin(url); http.setTimeout(WEATHER_TIMEOUT_MS);

  bool ok = false;
  if (http.GET() == 200) {
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, http.getString())) {
      weatherTemp      = doc["main"]["temp"].as<float>();
      weatherFeelsLike = doc["main"]["feels_like"].as<float>();
      weatherHum       = doc["main"]["humidity"].as<float>();
      weatherWindSpeed = doc["wind"]["speed"].as<float>();
      strlcpy(weatherDesc, doc["weather"][0]["description"] | "", sizeof(weatherDesc));
      strlcpy(weatherMain, doc["weather"][0]["main"] | "", sizeof(weatherMain));
      hasWeatherData = true;
      lastWeatherSuccess = millis();
      ok = true;
    }
  }
  http.end();
  return ok;
}

// ===== CONTAINER STATUS =====
bool fetchContainerStatus() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "http://" + String(NETDATA_HOST) + ":" + String(STATUS_PORT) + "/status";
  http.begin(url); http.setTimeout(NET_TIMEOUT_MS);

  bool ok = false;
  if (http.GET() == 200) {
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, http.getString())) {
      statusPihole      = doc["pihole"]      | false;
      statusImmich      = doc["immich"]      | false;
      statusNextcloud   = doc["nextcloud"]   | false;
      statusVaultwarden = doc["vaultwarden"] | false;
      ok = true;
    }
  }
  http.end();
  return ok;
}

// ===== FETCH ALL =====
bool fetchMetrics() {
  bool gotNetdata = false;
  bool gotHA = false;
  float v = 0;

  // CPU
  float cpuTotal = 0;
  int cpuSamples = 0;
  for (int i = 1; i <= 9; i++) {
    if (getNetdataValue("system.cpu", i, v)) {
      cpuTotal += (v > 0 ? v : 0);
      cpuSamples++;
    }
    otaYield();
  }
  if (cpuSamples > 0) {
    cpuUsage = cpuTotal;
    gotNetdata = true;
  }
  otaYield();

  // RAM
  float ramFree = 0, ramUsed_v = 0, ramCached = 0, ramBuffers = 0;
  if (getNetdataValue("system.ram", 1, ramFree) &&
      getNetdataValue("system.ram", 2, ramUsed_v) &&
      getNetdataValue("system.ram", 3, ramCached) &&
      getNetdataValue("system.ram", 4, ramBuffers)) {
    float ramTotal = ramFree + ramUsed_v + ramCached + ramBuffers;
    if (ramTotal > 0) {
      ramUsage = (ramUsed_v / ramTotal) * 100;
      gotNetdata = true;
    }
  }
  otaYield();

  float temp = 0;
  if (getNetdataValue("sensors.temperature_amdgpu-pci-0600_temp1_edge_input", 1, temp)) {
    temperature = temp;
    gotNetdata = true;
  }

  float storAvail = 0, storUsed = 0, storRes = 0;
  if (getNetdataValue("disk_space./host/mnt/storage", 1, storAvail) &&
      getNetdataValue("disk_space./host/mnt/storage", 2, storUsed) &&
      getNetdataValue("disk_space./host/mnt/storage", 3, storRes)) {
    usbStorageUsed = storUsed;
    usbStorageTotal = storAvail + storUsed + storRes;
    gotNetdata = true;
  }
  otaYield();

  float photAvail = 0, photUsed = 0, photRes = 0;
  if (getNetdataValue("disk_space./host/mnt/photos", 1, photAvail) &&
      getNetdataValue("disk_space./host/mnt/photos", 2, photUsed) &&
      getNetdataValue("disk_space./host/mnt/photos", 3, photRes)) {
    usbPhotosUsed = photUsed;
    usbPhotosTotal = photAvail + photUsed + photRes;
    gotNetdata = true;
  }

  float up = 0;
  if (getNetdataValue("system.uptime", 1, up) && up >= 0) {
    uptime = up;
    gotNetdata = true;
  }

  if (fetchContainerStatus()) gotNetdata = true;
  if (gotNetdata) lastMetricsSuccess = millis();
  otaYield();

  // HA Sensors — if getHAState fails (unavailable/offline), clear to NAN
  if (getHAState("sensor.bme680_air_sensor_bme680_temperature", v))  { livingTemp = v; gotHA = true; } else livingTemp = NAN;
  otaYield();
  if (getHAState("sensor.bme680_air_sensor_bme680_humidity", v))     { livingHum = v; gotHA = true; } else livingHum = NAN;
  otaYield();
  if (getHAState("sensor.bme680_air_sensor_bme680_iaq_0_500", v))    { livingIAQ = v; gotHA = true; } else livingIAQ = NAN;
  otaYield();
  if (getHAState("sensor.bme680_air_sensor_bme680_co2_eq_400_5000_ppm", v)) { livingCO2 = v; gotHA = true; } else livingCO2 = NAN;
  otaYield();
  if (getHAState("sensor.bme680_air_sensor_bme680_pressure", v))     { livingPressure = v; gotHA = true; } else livingPressure = NAN;
  otaYield();
  if (getHAState("sensor.living_room_clock_sensor_clock_lr_temperature", v)) { clockTemp = v; gotHA = true; } else clockTemp = NAN;
  otaYield();
  if (getHAState("sensor.living_room_clock_sensor_clock_lr_humidity", v))    { clockHum = v; gotHA = true; } else clockHum = NAN;
  otaYield();
  if (getHAState("sensor.baby_room_sensor_baby_room_temperature", v)) { babyTemp = v; gotHA = true; } else babyTemp = NAN;
  otaYield();
  if (getHAState("sensor.baby_room_sensor_baby_room_humidity", v))    { babyHum = v; gotHA = true; } else babyHum = NAN;
  otaYield();
  if (gotHA) lastHASuccess = millis();

  return gotNetdata || gotHA;
}

// ===== DRAW HELPERS =====
void drawBar(int x, int y, int w, int h, float pct, uint16_t color, uint16_t bg = CARD_COLOR) {
  if (pct > 100) pct = 100;
  if (pct < 0) pct = 0;
  tft.drawRect(x, y, w, h, LABEL_COLOR);
  int fill = (w - 4) * (pct / 100.0);
  if (fill > 0) tft.fillRect(x + 2, y + 2, fill, h - 4, color);
  if (fill < w - 4) tft.fillRect(x + 2 + fill, y + 2, (w - 4) - fill, h - 4, bg);
}

void printVal(float v, int decimals, const char* suffix) {
  if (isnan(v)) { tft.print("--"); }
  else { tft.print(v, decimals); tft.print(suffix); }
}

void drawDot(int x, int y, bool ok) {
  tft.fillCircle(x, y, 4, ok ? OK_COLOR : FAIL_COLOR);
}

uint16_t iaqColor(float iaq) {
  if (iaq < 50)  return 0x07E0;
  if (iaq < 100) return 0xFFE0;
  if (iaq < 150) return 0xFD20;
  return 0xF800;
}

void formatAge(unsigned long timestamp, char* out, size_t outSize) {
  if (timestamp == 0) {
    strlcpy(out, "n/a", outSize);
    return;
  }

  unsigned long ageSec = (millis() - timestamp) / 1000;
  if (ageSec < 60) snprintf(out, outSize, "%lus", ageSec);
  else if (ageSec < 3600) snprintf(out, outSize, "%lum", ageSec / 60);
  else snprintf(out, outSize, "%luh", ageSec / 3600);
}

// ===== LEFT COLUMN - PROXMOX =====
void drawProxmox() {
  // Card background
  tft.fillRect(PX_X, PX_Y, PX_W, PX_H, CARD_COLOR);

  int x = PX_X + PAD;
  int w = PX_W - PAD * 2;
  int y = PX_Y;

  // Title
  tft.fillRect(PX_X, y, PX_W, 22, TITLE_COLOR);
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(1);
  tft.setCursor(x, y + 7); tft.print("PROXMOX");
  y += 26;

  // CPU
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("CPU:");
  tft.setTextColor(CPU_COLOR);
  tft.setCursor(PX_X + PX_W - PAD - 30, y); tft.print(cpuUsage, 1); tft.print("%");
  drawBar(x, y + 12, w, 9, cpuUsage, CPU_COLOR);
  y += 26;

  // RAM
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("RAM:");
  tft.setTextColor(RAM_COLOR);
  tft.setCursor(PX_X + PX_W - PAD - 30, y); tft.print(ramUsage, 1); tft.print("%");
  drawBar(x, y + 12, w, 9, ramUsage, RAM_COLOR);
  y += 26;

  // Temp
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("TEMP:");
  tft.setTextColor(TEMP_COLOR); tft.setCursor(x + 40, y);
  if (temperature > 0) { tft.print(temperature, 1); tft.print("C"); }
  else tft.print("N/A");
  y += 18;

  tft.drawLine(x, y, PX_X + PX_W - PAD, y, DIVIDER_COLOR); y += 4;

  // USB Storage
  float storPct = (usbStorageTotal > 0) ? (usbStorageUsed / usbStorageTotal) * 100 : 0;
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("STG:");
  tft.setTextColor(USB_COLOR); tft.setCursor(x + 30, y);
  tft.print(usbStorageUsed, 0); tft.print("G");
  drawBar(x, y + 12, w, 9, storPct, USB_COLOR);
  y += 26;

  // USB Photos
  float photPct = (usbPhotosTotal > 0) ? (usbPhotosUsed / usbPhotosTotal) * 100 : 0;
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("PHO:");
  tft.setTextColor(USB_COLOR); tft.setCursor(x + 30, y);
  tft.print(usbPhotosUsed, 0); tft.print("G");
  drawBar(x, y + 12, w, 9, photPct, USB_COLOR);
  y += 26;

  tft.drawLine(x, y, PX_X + PX_W - PAD, y, DIVIDER_COLOR); y += 6;

  // Container status
  drawDot(x + 4, y + 5, statusImmich);
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 12, y); tft.print("Immich");
  drawDot(x + 80, y + 5, statusNextcloud);
  tft.setCursor(x + 88, y); tft.print("NC");
  y += 16;

  drawDot(x + 4, y + 5, statusPihole);
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 12, y); tft.print("Pihole");
  drawDot(x + 80, y + 5, statusVaultwarden);
  tft.setCursor(x + 88, y); tft.print("VW");
  y += 18;

  tft.drawLine(x, y, PX_X + PX_W - PAD, y, DIVIDER_COLOR); y += 4;

  // Uptime + last updated
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("UP:");
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 22, y);
  unsigned long days  = uptime / 86400;
  unsigned long hours = (uptime % 86400) / 3600;
  unsigned long mins  = (uptime % 3600) / 60;
  char uptimeStr[20];
  sprintf(uptimeStr, "%lud %luh %lum", days, hours, mins);
  tft.print(uptimeStr);
  tft.setTextColor(DIVIDER_COLOR);
  tft.setCursor(PX_X + PX_W - PAD - 28, y);
  char ageBuf[8];
  formatAge(lastMetricsSuccess, ageBuf, sizeof(ageBuf));
  tft.print(ageBuf);

  // Card border
  tft.drawRect(PX_X, PX_Y, PX_W, PX_H, DIVIDER_COLOR);
}

// ===== WEATHER CONDITION =====
const char* weatherConditionLabel(const char* main) {
  if (strcmp(main, "Clear") == 0)        return "SUN";
  if (strcmp(main, "Clouds") == 0)       return "CLOUD";
  if (strcmp(main, "Rain") == 0)         return "RAIN";
  if (strcmp(main, "Drizzle") == 0)      return "DRIZZLE";
  if (strcmp(main, "Thunderstorm") == 0) return "STORM";
  if (strcmp(main, "Snow") == 0)         return "SNOW";
  if (strcmp(main, "Mist") == 0)         return "MIST";
  if (strcmp(main, "Fog") == 0)          return "FOG";
  if (strcmp(main, "Haze") == 0)         return "HAZE";
  return main;
}

// ===== LEFT COLUMN - WEATHER =====
void drawWeather() {
  // Card background
  tft.fillRect(WX_X, WX_Y, WX_W, WX_H, CARD_COLOR);

  int x = WX_X + PAD;
  int y = WX_Y;

  // Title bar
  tft.fillRect(WX_X, y, WX_W, 22, WEATHER_COLOR);
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(1);
  tft.setCursor(x, y + 7); tft.print(OWM_CITY); tft.print(" WEATHER");
  y += 28;

  if (!hasWeatherData) {
    tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
    tft.setCursor(x, y); tft.print("Waiting weather API...");
    y += 16;
    tft.setCursor(x, y);
    if (WiFi.status() == WL_CONNECTED) tft.print("WiFi OK, no weather data");
    else tft.print("WiFi offline");
    tft.drawRect(WX_X, WX_Y, WX_W, WX_H, DIVIDER_COLOR);
    return;
  }

  // Condition prediction
  tft.setTextColor(TEMP_COLOR); tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(weatherConditionLabel(weatherMain));
  tft.setTextColor(LABEL_COLOR);
  tft.print(" "); tft.print(weatherDesc);
  y += 14;

  // Temp big
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  tft.setCursor(x, y);
  tft.print(weatherTemp, 1); tft.print("C");

  // Feels like
  tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
  tft.setCursor(x + 70, y + 4);
  tft.print("feels ");
  tft.setTextColor(TEXT_COLOR);
  tft.print(weatherFeelsLike, 1); tft.print("C");
  y += 20;

  // Humidity + Wind
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y);
  tft.print("Hum: ");
  tft.setTextColor(TEXT_COLOR); tft.print(weatherHum, 0); tft.print("%");
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x + 70, y);
  tft.print("Wind: ");
  tft.setTextColor(TEXT_COLOR); tft.print(weatherWindSpeed, 1); tft.print("m/s");

  // Card border
  tft.drawRect(WX_X, WX_Y, WX_W, WX_H, DIVIDER_COLOR);
}

// ===== RIGHT PANEL - SENSORS =====
void drawRightPanel() {
  int sx = RS_X + CARD_PAD;  // sub-card x (inside parent padding)
  int livingH = 128;
  int clockH = 50;
  int babyH = 50;
  int parentW = CARD_W + CARD_PAD * 2;
  int parentH = 22 + CARD_GAP + livingH + CARD_GAP + clockH + CARD_GAP + babyH + CARD_PAD;

  // Parent card background
  tft.fillRect(RS_X, RS_Y, parentW, parentH, CARD_COLOR);

  // Title bar
  tft.fillRect(RS_X, RS_Y, parentW, 22, TITLE_COLOR);
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(1);
  tft.setCursor(sx, RS_Y + 7); tft.print("HOME ASSISTANT");

  int y = RS_Y + 22 + CARD_GAP;

  // ── Living Room Card ──
  tft.fillRect(sx, y, CARD_W, livingH, CARD_COLOR);
  tft.drawRect(sx, y, CARD_W, livingH, DIVIDER_COLOR);

  tft.setTextColor(CYAN_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + CARD_PAD); tft.print("LIVING ROOM");

  if (isnan(livingTemp)) {
    tft.setTextColor(FAIL_COLOR); tft.setCursor(sx + CARD_PAD, y + 24);
    tft.print("OFFLINE");
  } else {
    tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
    tft.setCursor(sx + CARD_PAD, y + 20);
    printVal(livingTemp, 1, "C");
    tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
    tft.setCursor(sx + CARD_PAD, y + 42);
    printVal(livingHum, 1, "% RH");

    tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 56);
    tft.print("IAQ: ");
    tft.setTextColor(iaqColor(livingIAQ)); printVal(livingIAQ, 0, "");

    tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 70);
    tft.print("CO2: ");
    tft.setTextColor(TEXT_COLOR); printVal(livingCO2, 0, "ppm");

    tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 84);
    tft.print("Pres: ");
    tft.setTextColor(TEXT_COLOR); printVal(livingPressure, 1, "hPa");

    tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 98);
    tft.print("IAQ Level:");
    float iaqPct = isnan(livingIAQ) ? 0 : (livingIAQ / 500.0) * 100;
    drawBar(sx + CARD_PAD, y + 110, CARD_W - CARD_PAD * 2, 8, iaqPct, iaqColor(livingIAQ));
  }

  y += livingH + CARD_GAP;

  // ── Clock DHT11 Card ──
  tft.fillRect(sx, y, CARD_W, clockH, CARD_COLOR);
  tft.drawRect(sx, y, CARD_W, clockH, DIVIDER_COLOR);

  tft.setTextColor(TEMP_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + CARD_PAD); tft.print("CLOCK DHT11");

  if (isnan(clockTemp)) {
    tft.setTextColor(FAIL_COLOR); tft.setCursor(sx + CARD_PAD, y + 24);
    tft.print("OFFLINE");
  } else {
    tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
    tft.setCursor(sx + CARD_PAD, y + 20);
    printVal(clockTemp, 1, "C");
    tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
    tft.setCursor(sx + CARD_PAD, y + 38);
    printVal(clockHum, 1, "% RH");
  }

  y += clockH + CARD_GAP;

  // ── Baby Room Card ──
  tft.fillRect(sx, y, CARD_W, babyH, CARD_COLOR);
  tft.drawRect(sx, y, CARD_W, babyH, DIVIDER_COLOR);

  tft.setTextColor(PINK_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + CARD_PAD); tft.print("BABY ROOM");

  if (isnan(babyTemp)) {
    tft.setTextColor(FAIL_COLOR); tft.setCursor(sx + CARD_PAD, y + 24);
    tft.print("OFFLINE");
  } else {
    tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
    tft.setCursor(sx + CARD_PAD, y + 20);
    printVal(babyTemp, 1, "C");
    tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
    tft.setCursor(sx + CARD_PAD, y + 38);
    printVal(babyHum, 1, "% RH");
  }

  // Parent card border
  tft.drawRect(RS_X, RS_Y, parentW, parentH, DIVIDER_COLOR);
}

void drawSystemPanel() {
  tft.fillRect(SYS_X, SYS_Y, SYS_W, SYS_H, CARD_COLOR);
  tft.fillRect(SYS_X, SYS_Y, SYS_W, 22, TITLE_COLOR);

  int x = SYS_X + PAD;
  int y = SYS_Y + 7;

  tft.setTextColor(TEXT_COLOR); tft.setTextSize(1);
  tft.setCursor(x, y); tft.print("SYSTEM");

  y = SYS_Y + 28;
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("WiFi:");
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  tft.setTextColor(wifiOk ? OK_COLOR : FAIL_COLOR);
  tft.setCursor(x + 30, y); tft.print(wifiOk ? "ONLINE" : "OFFLINE");
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("RSSI:");
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 30, y);
  if (wifiOk) { tft.print(WiFi.RSSI()); tft.print(" dBm"); }
  else tft.print("n/a");
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("IP:");
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 30, y);
  if (wifiOk) tft.print(WiFi.localIP());
  else tft.print("0.0.0.0");
  y += 18;

  tft.drawLine(x, y, SYS_X + SYS_W - PAD, y, DIVIDER_COLOR);
  y += 6;

  char ageBuf[8];
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("Proxmox:");
  formatAge(lastMetricsSuccess, ageBuf, sizeof(ageBuf));
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 50, y); tft.print(ageBuf);
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("HA:");
  formatAge(lastHASuccess, ageBuf, sizeof(ageBuf));
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 50, y); tft.print(ageBuf);
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("Weather:");
  formatAge(lastWeatherSuccess, ageBuf, sizeof(ageBuf));
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 50, y); tft.print(ageBuf);
  y += 18;

  tft.drawLine(x, y, SYS_X + SYS_W - PAD, y, DIVIDER_COLOR);
  y += 6;

  time_t now = time(nullptr);
  char timeBuf[16] = "--:--:--";
  if (now > 1000000000) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
  }
  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("Local time: ");
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 66, y); tft.print(timeBuf);
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y);
  tft.print("Sleep ");
  if (SLEEP_HOUR < 10) tft.print("0");
  tft.print(SLEEP_HOUR);
  tft.print(":00-");
  if (WAKE_HOUR < 10) tft.print("0");
  tft.print(WAKE_HOUR);
  tft.print(":00");
  y += 14;

  tft.setTextColor(LABEL_COLOR); tft.setCursor(x, y); tft.print("Upd:");
  tft.setTextColor(TEXT_COLOR); tft.setCursor(x + 30, y);
  tft.print(UPDATE_INTERVAL / 1000); tft.print("s / ");
  tft.print(WEATHER_INTERVAL / 60000); tft.print("m");

  tft.drawRect(SYS_X, SYS_Y, SYS_W, SYS_H, DIVIDER_COLOR);
}

// ===== DISPLAY =====
void displayMetrics() {
  if (firstRun) {
    tft.fillScreen(BG_COLOR);
    firstRun = false;
  }
  drawProxmox();
  drawWeather();
  drawRightPanel();
  drawSystemPanel();
}

// ===== NTP =====
bool syncTime() {
  configTzTime(TIMEZONE_STRING, "pool.ntp.org", "time.nist.gov");
  int retry = 0;
  while (time(nullptr) < 1000000000 && retry < 20) {
    delay(500); retry++;
  }
  return time(nullptr) > 1000000000;
}

int currentHour() {
  time_t now = time(nullptr);
  if (now < 1000000000) return WAKE_HOUR;
  struct tm t;
  if (localtime_r(&now, &t) == nullptr) return WAKE_HOUR;
  return t.tm_hour;
}

bool isNightTime() {
  int h = currentHour();
  // Handles overnight range: 23:00 → 08:59
  if (SLEEP_HOUR > WAKE_HOUR)
    return h >= SLEEP_HOUR || h < WAKE_HOUR;
  return h >= SLEEP_HOUR && h < WAKE_HOUR;
}

void handleSleep() {
  bool shouldSleep = isNightTime();

  if (shouldSleep && !isSleeping) {
    tft.fillScreen(BG_COLOR);
    tft.writecommand(0x28);  // display off
    digitalWrite(TFT_BL, LOW);  // backlight off
    isSleeping = true;
    firstRun = true;  // force full redraw on wake
  }
  else if (!shouldSleep && isSleeping) {
    digitalWrite(TFT_BL, HIGH);  // backlight on
    tft.writecommand(0x29);  // display on
    isSleeping = false;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.onEvent(onWiFiEvent);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);

  bool wifiConnected = connectWiFi(true);
  lastWiFiRetry = millis();

  if (wifiConnected) {
    ensureOTAReady();

    if (syncTime()) lastTimeSync = millis();
    fetchMetrics();
    lastUpdate = millis();
    fetchWeather();
    lastWeatherUpdate = millis();
  }

  displayMetrics();
  delay(500);
}

// ===== LOOP =====
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ensureOTAReady();
    otaYield();
  }

  handleSleep();
  if (isSleeping) { delay(250); return; }

  if (WiFi.status() != WL_CONNECTED && (millis() - lastWiFiRetry > wifiRetryInterval)) {
    lastWiFiRetry = millis();
    connectWiFi(false);
  }

  if (millis() - lastUpdate > UPDATE_INTERVAL) {
    if (WiFi.status() == WL_CONNECTED) fetchMetrics();
    displayMetrics();
    lastUpdate = millis();
  }

  if (WiFi.status() == WL_CONNECTED &&
      (lastWeatherUpdate == 0 || millis() - lastWeatherUpdate > WEATHER_INTERVAL)) {
    fetchWeather();
    lastWeatherUpdate = millis();
  }

  if (WiFi.status() == WL_CONNECTED &&
      (lastTimeSync == 0 || millis() - lastTimeSync > NTP_RESYNC_INTERVAL)) {
    if (syncTime()) lastTimeSync = millis();
  }

  delay(100);
}
