#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// ===== CONFIG =====
#include "../config/secrets.h"
#define OWM_CITY        "Porto"
#define OWM_COUNTRY     "PT"
#define UPDATE_INTERVAL 5000
#define WEATHER_INTERVAL 600000  // 10 minutes for weather
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

// ===== HELPERS =====
void drawCentered(const char* text, int y) {
  int16_t tw = strlen(text) * 6 * tft.textsize;
  tft.setCursor((SCR_W - tw) / 2, y);
  tft.print(text);
}

// ===== WIFI =====
void connectWiFi() {
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  drawCentered("Connecting WiFi...", SCR_H / 2 - 10);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    tft.fillScreen(BG_COLOR);
    drawCentered("WiFi Connected!", SCR_H / 2 - 10);
    delay(800);
  }
}

// ===== NETDATA =====
float getNetdataValue(String chart, int valueIndex = 1) {
  HTTPClient http;
  String url = "http://" + String(NETDATA_HOST) + ":" + String(NETDATA_PORT) +
               "/api/v1/data?chart=" + chart + "&after=-1&format=json";
  http.begin(url); http.setTimeout(3000);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(8192);
    if (!deserializeJson(doc, http.getString())) {
      JsonArray data = doc["data"];
      if (data.size() > 0) {
        JsonArray values = data[0];
        if (values.size() > valueIndex) {
          float value = values[valueIndex];
          http.end(); return value;
        }
      }
    }
  }
  http.end(); return -1;
}

// ===== HA API =====
float getHAState(const char* entity_id) {
  HTTPClient http;
  String url = "http://" + String(HA_HOST) + ":" + String(HA_PORT) +
               "/api/states/" + String(entity_id);
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, http.getString())) {
      float value = doc["state"].as<float>();
      http.end(); return value;
    }
  }
  http.end(); return -1;
}

// ===== WEATHER API =====
void fetchWeather() {
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" +
               String(OWM_CITY) + "," + String(OWM_COUNTRY) +
               "&appid=" + String(OWM_API_KEY) +
               "&units=metric";
  http.begin(url); http.setTimeout(5000);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, http.getString())) {
      weatherTemp      = doc["main"]["temp"].as<float>();
      weatherFeelsLike = doc["main"]["feels_like"].as<float>();
      weatherHum       = doc["main"]["humidity"].as<float>();
      weatherWindSpeed = doc["wind"]["speed"].as<float>();
      strlcpy(weatherDesc, doc["weather"][0]["description"] | "", sizeof(weatherDesc));
      strlcpy(weatherMain, doc["weather"][0]["main"] | "", sizeof(weatherMain));
    }
  }
  http.end();
}

// ===== CONTAINER STATUS =====
void fetchContainerStatus() {
  HTTPClient http;
  String url = "http://" + String(NETDATA_HOST) + ":" + String(STATUS_PORT) + "/status";
  http.begin(url); http.setTimeout(3000);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, http.getString())) {
      statusPihole      = doc["pihole"]      | false;
      statusImmich      = doc["immich"]      | false;
      statusNextcloud   = doc["nextcloud"]   | false;
      statusVaultwarden = doc["vaultwarden"] | false;
    }
  }
  http.end();
}

// ===== FETCH ALL =====
void fetchMetrics() {
  // CPU
  float cpuTotal = 0;
  for (int i = 1; i <= 9; i++) {
    float val = getNetdataValue("system.cpu", i);
    if (val > 0) cpuTotal += val;
  }
  if (cpuTotal > 0) cpuUsage = cpuTotal;

  // RAM
  float ramFree = getNetdataValue("system.ram", 1);
  float ramUsed_v = getNetdataValue("system.ram", 2);
  float ramCached = getNetdataValue("system.ram", 3);
  float ramBuffers = getNetdataValue("system.ram", 4);
  if (ramUsed_v > 0) {
    float ramTotal = ramFree + ramUsed_v + ramCached + ramBuffers;
    ramUsage = (ramUsed_v / ramTotal) * 100;
  }

  float temp = getNetdataValue("sensors.temperature_amdgpu-pci-0600_temp1_edge_input", 1);
  if (temp > 0) temperature = temp;

  float storAvail = getNetdataValue("disk_space./host/mnt/storage", 1);
  float storUsed  = getNetdataValue("disk_space./host/mnt/storage", 2);
  float storRes   = getNetdataValue("disk_space./host/mnt/storage", 3);
  if (storUsed >= 0 && storAvail >= 0) {
    usbStorageUsed = storUsed;
    usbStorageTotal = storAvail + storUsed + storRes;
  }

  float photAvail = getNetdataValue("disk_space./host/mnt/photos", 1);
  float photUsed  = getNetdataValue("disk_space./host/mnt/photos", 2);
  float photRes   = getNetdataValue("disk_space./host/mnt/photos", 3);
  if (photUsed >= 0 && photAvail >= 0) {
    usbPhotosUsed = photUsed;
    usbPhotosTotal = photAvail + photUsed + photRes;
  }

  float up = getNetdataValue("system.uptime", 1);
  if (up > 0) uptime = up;

  fetchContainerStatus();

  // HA Sensors
  float v;
  v = getHAState("sensor.bme680_air_sensor_bme680_temperature");  if (v > 0) livingTemp = v;
  v = getHAState("sensor.bme680_air_sensor_bme680_humidity");     if (v > 0) livingHum = v;
  v = getHAState("sensor.bme680_air_sensor_bme680_iaq_0_500");    if (v > 0) livingIAQ = v;
  v = getHAState("sensor.bme680_air_sensor_bme680_co2_eq_400_5000_ppm"); if (v > 0) livingCO2 = v;
  v = getHAState("sensor.bme680_air_sensor_bme680_pressure");     if (v > 0) livingPressure = v;
  v = getHAState("sensor.living_room_clock_sensor_clock_lr_temperature");  if (v > 0) clockTemp = v;
  v = getHAState("sensor.living_room_clock_sensor_clock_lr_humidity");     if (v > 0) clockHum = v;
  v = getHAState("sensor.baby_room_sensor_baby_room_temperature"); if (v > 0) babyTemp = v;
  v = getHAState("sensor.baby_room_sensor_baby_room_humidity");    if (v > 0) babyHum = v;
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

void drawDot(int x, int y, bool ok) {
  tft.fillCircle(x, y, 4, ok ? OK_COLOR : FAIL_COLOR);
}

uint16_t iaqColor(float iaq) {
  if (iaq < 50)  return 0x07E0;
  if (iaq < 100) return 0xFFE0;
  if (iaq < 150) return 0xFD20;
  return 0xF800;
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
  tft.setCursor(PX_X + PX_W - PAD - 18, y);
  tft.print(((millis() - lastUpdate) / 1000)); tft.print("s");

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
  tft.setCursor(x, y + 7); tft.print("PORTO WEATHER");
  y += 28;

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

  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  tft.setCursor(sx + CARD_PAD, y + 20);
  tft.print(livingTemp, 1); tft.print("C");
  tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + 42);
  tft.print(livingHum, 1); tft.print("% RH");

  tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 56);
  tft.print("IAQ: ");
  tft.setTextColor(iaqColor(livingIAQ)); tft.print(livingIAQ, 0);

  tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 70);
  tft.print("CO2: ");
  tft.setTextColor(TEXT_COLOR); tft.print(livingCO2, 0); tft.print("ppm");

  tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 84);
  tft.print("Pres: ");
  tft.setTextColor(TEXT_COLOR); tft.print(livingPressure, 1); tft.print("hPa");

  tft.setTextColor(LABEL_COLOR); tft.setCursor(sx + CARD_PAD, y + 98);
  tft.print("IAQ Level:");
  float iaqPct = (livingIAQ / 500.0) * 100;
  drawBar(sx + CARD_PAD, y + 110, CARD_W - CARD_PAD * 2, 8, iaqPct, iaqColor(livingIAQ));

  y += livingH + CARD_GAP;

  // ── Clock DHT11 Card ──
  tft.fillRect(sx, y, CARD_W, clockH, CARD_COLOR);
  tft.drawRect(sx, y, CARD_W, clockH, DIVIDER_COLOR);

  tft.setTextColor(TEMP_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + CARD_PAD); tft.print("CLOCK DHT11");

  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  tft.setCursor(sx + CARD_PAD, y + 20);
  tft.print(clockTemp, 1); tft.print("C");
  tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + 38);
  tft.print(clockHum, 1); tft.print("% RH");

  y += clockH + CARD_GAP;

  // ── Baby Room Card ──
  tft.fillRect(sx, y, CARD_W, babyH, CARD_COLOR);
  tft.drawRect(sx, y, CARD_W, babyH, DIVIDER_COLOR);

  tft.setTextColor(PINK_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + CARD_PAD); tft.print("BABY ROOM");

  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  tft.setCursor(sx + CARD_PAD, y + 20);
  tft.print(babyTemp, 1); tft.print("C");
  tft.setTextColor(LABEL_COLOR); tft.setTextSize(1);
  tft.setCursor(sx + CARD_PAD, y + 38);
  tft.print(babyHum, 1); tft.print("% RH");

  // Parent card border
  tft.drawRect(RS_X, RS_Y, parentW, parentH, DIVIDER_COLOR);
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
}

// ===== NTP =====
void syncTime() {
  configTzTime(TIMEZONE_STRING, "pool.ntp.org", "time.nist.gov");
  int retry = 0;
  while (time(nullptr) < 1000000000 && retry < 20) {
    delay(500); retry++;
  }
}

int currentHour() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return t->tm_hour;
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
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);
  connectWiFi();
  syncTime();
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TEXT_COLOR); tft.setTextSize(2);
  drawCentered("Fetching data...", SCR_H / 2 - 10);
  fetchWeather();
  lastWeatherUpdate = millis();
  delay(500);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}

// ===== LOOP =====
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  handleSleep();
  if (isSleeping) { delay(10000); return; }

  if (millis() - lastUpdate > UPDATE_INTERVAL) {
    fetchMetrics();
    displayMetrics();
    lastUpdate = millis();
  }

  if (millis() - lastWeatherUpdate > WEATHER_INTERVAL) {
    fetchWeather();
    lastWeatherUpdate = millis();
  }

  delay(100);
}
