#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

// ===== CONFIG =====
#include "../config/secrets.h"

#define DEVICE_ID "baby_room_dht22"  // Changed ID
#define STATE_TOPIC "home/baby-room/sensor/state"
#define AVAIL_TOPIC "home/baby-room/sensor/status"

#define DHTPIN 5
#define DHTTYPE DHT22
#define LED_PIN 8  // Built-in blue LED on ESP32-C3 Mini

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastPublish = 0;
bool discoverySent = false;

// ===== LED FUNCTIONS =====
void ledBlink(int times, int delayMs = 100) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);  // LED ON
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);   // LED OFF
    delay(delayMs);
  }
}

void ledOn() {
  digitalWrite(LED_PIN, HIGH);
}

void ledOff() {
  digitalWrite(LED_PIN, LOW);
}

void ledBlinkFast() {
  ledBlink(1, 50);  // Quick blink
}

// ===== DISCOVERY =====
void publishDiscovery() {
  Serial.println("📡 Publishing discovery...");
  
  auto send = [&](String id, String name, String field, String unit, String devClass="") {
    String topic = "homeassistant/sensor/" + id + "/config";

    String payload = "{";
    payload += "\"name\":\"" + name + "\",";
    payload += "\"stat_t\":\"" STATE_TOPIC "\",";
    payload += "\"avty_t\":\"" AVAIL_TOPIC "\",";
    payload += "\"pl_avail\":\"online\",";
    payload += "\"pl_not_avail\":\"offline\",";
    payload += "\"val_tpl\":\"{{ value_json." + field + " }}\",";
    if (unit.length()) payload += "\"unit_of_meas\":\"" + unit + "\",";
    if (devClass.length()) payload += "\"dev_cla\":\"" + devClass + "\",";
    payload += "\"frc_upd\":true,";
    payload += "\"uniq_id\":\"" + id + "\",";
    payload += "\"dev\":{";
    payload += "\"ids\":[\"" DEVICE_ID "\"],";
    payload += "\"name\":\"Baby Room Sensor\",";
    payload += "\"mf\":\"DIY\",";
    payload += "\"mdl\":\"C3+DHT22\"";
    payload += "}}";

    Serial.print("  Size: ");
    Serial.print(payload.length());
    Serial.print("B - ");
    
    bool success = mqtt.publish(topic.c_str(), payload.c_str(), true);
    Serial.println(name + ": " + (success ? "✅" : "❌"));
    
    delay(100);
  };

  send("babyroom_temperature", "Baby Room Temperature", "temperature", "°C", "temperature");
  send("babyroom_humidity", "Baby Room Humidity", "humidity", "%", "humidity");
  send("babyroom_wifi", "Baby Room WiFi Signal", "rssi", "dBm", "signal_strength");
  send("babyroom_uptime", "Baby Room Uptime", "uptime", "min");

  discoverySent = true;
  Serial.println("✅ Discovery done!");
}

// ===== WIFI CONNECT =====
void connectWiFi() {
  Serial.print("🔌 Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(400);
    ledBlinkFast();  // Blink while connecting
    Serial.print(".");
    delay(100);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    ledBlink(2, 100);  // 2 quick blinks = success
    Serial.println("\n✅ WiFi connected!");
    Serial.print("📍 IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("📶 RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    ledBlink(5, 200);  // 5 slow blinks = failed
    Serial.println("\n❌ WiFi failed!");
  }
}

// ===== MQTT CONNECT =====
void connectMQTT() {
  Serial.print("🔗 Connecting to MQTT broker");
  
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    ledBlinkFast();
    Serial.print(".");
    
    // LWT (Last Will Testament) - sends "offline" when device disconnects unexpectedly
    bool connected = mqtt.connect(
      DEVICE_ID,
      MQTT_USER,
      MQTT_PASS,
      AVAIL_TOPIC,      // Will topic
      1,                // Will QoS
      true,             // Will retain
      "offline"         // Will message
    );
    
    if (connected) {
      ledBlink(2, 100);  // 2 quick blinks = success
      Serial.println("\n✅ MQTT connected!");
      
      // Send online status
      mqtt.publish(AVAIL_TOPIC, "online", true);
      
      // Send discovery only once after boot
      if (!discoverySent) {
        delay(500);  // Wait for broker to process online status
        publishDiscovery();
      }
      return;
    } else {
      Serial.print(" (rc=");
      Serial.print(mqtt.state());
      Serial.print(")");
      delay(2000);
      attempts++;
    }
  }
  
  if (!mqtt.connected()) {
    ledBlink(5, 200);  // 5 slow blinks = failed
    Serial.println("\n❌ MQTT failed!");
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  ledOff();
  ledBlink(3, 200);  // 3 blinks on startup
  
  Serial.println("\n\n🚀 ESP32-C3 DHT22 Baby Room Sensor");
  Serial.println("====================================");
  
  // Initialize DHT22
  Serial.println("🌡️  Initializing DHT22...");
  dht.begin();
  delay(2000);  // DHT22 needs time to stabilize
  Serial.println("✅ DHT22 ready!");
  
  // Connect WiFi
  connectWiFi();
  
  // Configure MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(15);  // Send keepalive every 15 seconds
  
  // Connect MQTT
  connectMQTT();
  
  Serial.println("\n🎯 Setup complete! Starting measurements...\n");
}

// ===== LOOP =====
void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi disconnected! Reconnecting...");
    connectWiFi();
  }
  
  // Reconnect MQTT if needed
  if (!mqtt.connected()) {
    Serial.println("⚠️  MQTT disconnected! Reconnecting...");
    connectMQTT();
  }
  
  mqtt.loop();

  // Publish every 30 seconds
  if (millis() - lastPublish > 30000) {
    Serial.println("📊 Reading DHT22...");
    
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    Serial.print("  Temperature: ");
    Serial.print(t);
    Serial.println(" °C");
    Serial.print("  Humidity: ");
    Serial.print(h);
    Serial.println(" %");

    if (!isnan(t) && !isnan(h)) {
      ledOn();  // LED on while publishing
      
      // Calculate uptime in minutes
      unsigned long uptimeMinutes = millis() / 60000;
      
      String payload = "{";
      payload += "\"temperature\":" + String(t, 1) + ",";
      payload += "\"humidity\":" + String(h, 1) + ",";
      payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      payload += "\"uptime\":" + String(uptimeMinutes);  // Minutes
      payload += "}";

      bool success = mqtt.publish(STATE_TOPIC, payload.c_str(), false);
      
      ledOff();  // LED off after publishing
      
      if (success) {
        ledBlinkFast();  // Quick blink on success
        Serial.println("✅ Published to MQTT!");
        Serial.print("  Uptime: ");
        Serial.print(uptimeMinutes);
        Serial.println(" min");
        Serial.println("  " + payload);
      } else {
        ledBlink(3, 100);  // 3 blinks = publish failed
        Serial.println("❌ MQTT publish failed!");
      }
    } else {
      ledBlink(5, 100);  // 5 blinks = sensor read failed
      Serial.println("❌ Failed to read DHT22! Check wiring!");
    }

    lastPublish = millis();
    Serial.println();
  }
}