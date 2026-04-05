#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include "bsec.h"

// ===== CONFIG =====
#include "../config/secrets.h"
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
const char* mqtt_server = MQTT_HOST;
const int mqtt_port = MQTT_PORT;
const char* mqtt_topic = "bme680/data";
const char* lwt_topic = "bme680/status";

// ===== LEDS =====
#define LED_OK 2        // blue LED
#define LED_ERROR 5     // red LED

// ===== OBJECTS =====
WiFiClient espClient;
PubSubClient client(espClient);
Bsec iaqSensor;

// ===== TIMERS =====
unsigned long lastPublish = 0;
unsigned long lastMQTTSeen = 0;
unsigned long lastReconnectAttempt = 0;

// ===== EEPROM =====
#define EEPROM_SIZE 1024

// ===== RECONNECT SETTINGS =====
#define RECONNECT_INTERVAL 5000
int wifiRetries = 0;
int mqttRetries = 0;
#define MAX_WIFI_RETRIES 30
#define MAX_MQTT_RETRIES 5

// ===== UNIQUE CLIENT ID =====
String getChipId() {
  uint32_t id = 0;
  for (int i = 0; i < 17; i++) {
    id = (id << 1) | ((ESP.getEfuseMac() >> i) & 1);
  }
  return String("ESP32-BME680-") + String(id);
}

// ===== CONNECT WIFI WITH TIMEOUT =====
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  
  Serial.println("WiFi connecting...");
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  
  wifiRetries = 0;
  while (WiFi.status() != WL_CONNECTED && wifiRetries < MAX_WIFI_RETRIES) {
    Serial.print(".");
    digitalWrite(LED_OK, !digitalRead(LED_OK)); // blink blue
    delay(500);
    wifiRetries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_OK, HIGH);
    digitalWrite(LED_ERROR, LOW);
    wifiRetries = 0;
    return true;
  } else {
    Serial.println("\n✗ WiFi FAILED - will retry");
    digitalWrite(LED_ERROR, HIGH);
    return false;
  }
}

// ===== MQTT RECONNECT WITH TIMEOUT =====
bool reconnectMQTT() {
  if (client.connected()) return true;

  Serial.print("MQTT connecting... ");

  if (client.connect(
        getChipId().c_str(),
        MQTT_USER,             // username
        MQTT_PASS,             // password
        lwt_topic,             // LWT topic
        0,                     // QoS
        true,                  // retain
        "offline"              // LWT message
      ))
  {
    Serial.println("✓ OK");
    client.publish(lwt_topic, "online", true);
    digitalWrite(LED_ERROR, LOW);
    mqttRetries = 0;
    return true;
  } 
  else {
    Serial.print("✗ FAILED rc=");
    Serial.println(client.state());
    digitalWrite(LED_ERROR, HIGH);
    mqttRetries++;

    if (mqttRetries >= MAX_MQTT_RETRIES) {
      Serial.println("Too many MQTT failures - REBOOTING");
      delay(1000);
      ESP.restart();
    }
    return false;
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n===== BME680 SENSOR STARTING =====");

  pinMode(LED_OK, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_OK, LOW);
  digitalWrite(LED_ERROR, LOW);

  // EEPROM init
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("✗ EEPROM initialization FAILED!");
  } else {
    Serial.println("✓ EEPROM initialized");
  }

  // I2C init (ESP32 WROOM: SDA=21, SCL=22)
  Wire.begin(21, 22);
  Serial.println("✓ I2C initialized");

  // WiFi connect
  if (!connectWiFi()) {
    Serial.println("WiFi failed at boot - REBOOTING");
    delay(2000);
    ESP.restart();
  }

  // MQTT setup
  client.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();

  // Publish Home Assistant Discovery
  Serial.println("CALLING DISCOVERY...");
  publishDiscovery();
  Serial.println("AFTER DISCOVERY CALL");

  // BSEC init
  Serial.println("Initializing BME680...");
  iaqSensor.begin(BME68X_I2C_ADDR_LOW, Wire); // 0x76

  if (iaqSensor.bsecStatus != BSEC_OK) {
    Serial.print("✗ BSEC error code: ");
    Serial.println(iaqSensor.bsecStatus);
    digitalWrite(LED_ERROR, HIGH);
    delay(3000);
    ESP.restart();
  }

  Serial.println("✓ BME680 initialized");

  // Restore BSEC state if exists
  Serial.print("Checking EEPROM for saved state... ");
  if (EEPROM.read(0) == 0xA5) {
    Serial.println("Found!");
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    for (int i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
      state[i] = EEPROM.read(i + 1);
    }
    iaqSensor.setState(state);
    Serial.println("✓ Restored BSEC state from EEPROM");
  } else {
    Serial.println("Not found");
    Serial.println("✗ No saved BSEC state (first boot or EEPROM empty)");
  }

  // Subscribe to BSEC sensors
  bsec_virtual_sensor_t sensorList[] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_GAS_PERCENTAGE,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS
  };

  iaqSensor.updateSubscription(sensorList, 11, BSEC_SAMPLE_RATE_LP);

  Serial.println("✓ BME680 + BSEC Ready");
  Serial.println("=====================================\n");
  
  lastReconnectAttempt = millis();
}

// ================== LOOP ==================
void loop() {
  // WiFi check (non-blocking)
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      
      if (!connectWiFi()) {
        static int wifiFailCount = 0;
        wifiFailCount++;
        if (wifiFailCount >= 5) {
          Serial.println("WiFi dead - REBOOTING");
          ESP.restart();
        }
      } else {
        reconnectMQTT();
      }
    }
    return; // Skip rest if no WiFi
  }

  // MQTT check (non-blocking)
  if (!client.connected()) {
    if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      reconnectMQTT();
    }
    return; // Skip publishing if no MQTT
  }

  client.loop();

  // BSEC must run every loop
  if (iaqSensor.run()) {
    lastMQTTSeen = millis();

    // Debug to Serial
    Serial.print("IAQ: ");
    Serial.print(iaqSensor.iaq);
    Serial.print("  Acc: ");
    Serial.print(iaqSensor.iaqAccuracy);
    Serial.print("  Temp: ");
    Serial.print(iaqSensor.temperature);
    Serial.print("°C  Hum: ");
    Serial.print(iaqSensor.humidity);
    Serial.print("%  Gas: ");
    Serial.print(iaqSensor.gasResistance);
    Serial.println(" Ω");

    // Publish every 30 seconds
    if (millis() - lastPublish >= 30000) {
      publishData();
      lastPublish = millis();

      // Quick LED flash on publish
      digitalWrite(LED_OK, LOW);
      delay(50);
      digitalWrite(LED_OK, HIGH);
    }
  } else {
    // Check BSEC status
    if (iaqSensor.bsecStatus != BSEC_OK) {
      Serial.print("BSEC error: ");
      Serial.println(iaqSensor.bsecStatus);
      digitalWrite(LED_ERROR, HIGH);
    }
    if (iaqSensor.bme68xStatus != BME68X_OK) {
      Serial.print("BME680 error: ");
      Serial.println(iaqSensor.bme68xStatus);
      digitalWrite(LED_ERROR, HIGH);
    }
  }

  // MQTT watchdog - restart if no data for 120 seconds
  if (millis() - lastMQTTSeen > 120000) {
    Serial.println("BSEC timeout - REBOOTING");
    ESP.restart();
  }
}

// ================== PUBLISH ==================
void publishData() {
  String payload = "{";
  payload += "\"iaq\":" + String(iaqSensor.iaq, 2);
  payload += ",\"iaqAccuracy\":" + String(iaqSensor.iaqAccuracy);
  payload += ",\"staticIaq\":" + String(iaqSensor.staticIaq, 2);
  payload += ",\"co2\":" + String(iaqSensor.co2Equivalent, 2);
  payload += ",\"voc\":" + String(iaqSensor.breathVocEquivalent, 2);
  payload += ",\"temp\":" + String(iaqSensor.temperature, 2);
  payload += ",\"hum\":" + String(iaqSensor.humidity, 2);
  payload += ",\"pressure\":" + String(iaqSensor.pressure / 100.0, 2);
  payload += ",\"gas_res\":" + String(iaqSensor.gasResistance, 2);
  payload += ",\"gas_pct\":" + String(iaqSensor.gasPercentage, 2);
  payload += ",\"stab_status\":" + String(iaqSensor.stabStatus);
  payload += ",\"runin_status\":" + String(iaqSensor.runInStatus);
  payload += "}";

  if (client.publish(mqtt_topic, payload.c_str())) {
    Serial.print("✓ MQTT published → ");
    Serial.println(payload);
    digitalWrite(LED_ERROR, LOW);
  } else {
    Serial.println("✗ MQTT publish FAILED");
    digitalWrite(LED_ERROR, HIGH);
  }

  saveBSECState();
}

// ================== SAVE BSEC STATE ==================
void saveBSECState() {
  uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
  iaqSensor.getState(state);

  EEPROM.write(0, 0xA5);  // Marker byte
  for (int i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
    EEPROM.write(i + 1, state[i]);
  }
  
  if (EEPROM.commit()) {
    Serial.println("✓ BSEC state saved to EEPROM");
  } else {
    Serial.println("✗ EEPROM commit FAILED!");
    digitalWrite(LED_ERROR, HIGH);
  }
}

void publishDiscovery() {
  Serial.println(">>> DISCOVERY START");
  client.publish("homeassistant/test", "hello", true);
  auto send = [&](String id, String name, String field, String unit, String dev_class = "") {
    String topic = "homeassistant/sensor/" + id + "/config";

    String payload = "{";
    payload += "\"name\":\"" + name + "\",";
    payload += "\"state_topic\":\"bme680/data\",";
    payload += "\"value_template\":\"{{ value_json." + field + " }}\",";
    if (unit.length() > 0)
      payload += "\"unit_of_measurement\":\"" + unit + "\",";
    if (dev_class.length() > 0)
      payload += "\"device_class\":\"" + dev_class + "\",";
    payload += "\"unique_id\":\"" + id + "\",";
    payload += "\"availability_topic\":\"bme680/status\",";
    payload += "\"payload_available\":\"online\",";
    payload += "\"payload_not_available\":\"offline\",";
    payload += "\"device\":{";
    payload += "\"identifiers\":[\"bme680_sensor\"],";
    payload += "\"name\":\"BME680 Air Sensor\",";
    payload += "\"manufacturer\":\"Custom\",";
    payload += "\"model\":\"ESP32 BME680\"";
    payload += "}";
    payload += "}";

    client.publish(topic.c_str(), payload.c_str(), true);
  };

  send("bme680_iaq_0_500",                 "BME680 IAQ (0–500)",                 "iaq",          "IAQ");
  send("bme680_iaq_accuracy_0_3",          "BME680 IAQ Accuracy (0–3)",          "iaqAccuracy",  "");
  send("bme680_co2_eq_400_5000_ppm",       "BME680 CO₂ eq. (400–5000 ppm)",      "co2",          "ppm");
  send("bme680_voc_eq_0_1000_ppm",         "BME680 VOC eq. (0–1000 ppm)",        "voc",          "ppm");
  send("bme680_temperature",               "BME680 Temperature",                 "temp",         "°C");
  send("bme680_humidity",                  "BME680 Humidity",                    "hum",          "%",  "humidity");
  send("bme680_pressure",                  "BME680 Pressure",                    "pressure",     "hPa");
  send("bme680_gas_percentage_0_100",      "BME680 Gas Percentage (0–100%)",     "gas_pct",      "%");
}