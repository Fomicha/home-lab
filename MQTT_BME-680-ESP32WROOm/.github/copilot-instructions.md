# Copilot Instructions for MQTT_BME-680-ESP32WROOm

## Project Overview
This project is an Arduino-based implementation for the ESP32 microcontroller, integrating the BME680 environmental sensor. It uses WiFi and MQTT protocols to transmit sensor data to a specified MQTT broker. Key features include:

- **WiFi Connectivity**: Connects to a specified WiFi network with retry logic.
- **MQTT Communication**: Publishes sensor data to an MQTT topic and handles Last Will and Testament (LWT) for device status.
- **BME680 Sensor Integration**: Uses the BSEC library for advanced sensor data processing.
- **LED Indicators**: Blue LED for successful operations, red LED for errors.

## Key Files
- `MQTT_BME-680-ESP32WROOm.ino`: Main Arduino sketch containing all logic for WiFi, MQTT, and sensor operations.

## Developer Workflows

### Building and Uploading
1. Open the `.ino` file in the Arduino IDE.
2. Select the correct board (ESP32) and port from the Tools menu.
3. Click the Upload button to flash the code to the ESP32.

### Debugging
- Use the Serial Monitor (baud rate: 115200) to view debug logs.
- Check LED indicators:
  - Blue LED blinks during WiFi connection attempts.
  - Red LED lights up on connection failure.

### Testing MQTT
- Use an MQTT client (e.g., `mosquitto_sub`) to subscribe to the `bme680/data` topic and verify published messages.
- Ensure the MQTT broker is running and accessible at the configured IP and port.

## Project-Specific Conventions
- **WiFi Retry Logic**: The device attempts to connect to WiFi up to `MAX_WIFI_RETRIES` times before failing.
- **MQTT Retry Logic**: Similar retry mechanism for MQTT connections, defined by `MAX_MQTT_RETRIES`.
- **EEPROM Usage**: Reserved 1024 bytes for storing persistent data.
- **Unique Client ID**: Generated dynamically using the ESP32's MAC address.

## External Dependencies
- **Libraries**:
  - `WiFi.h`: For WiFi connectivity.
  - `PubSubClient.h`: For MQTT communication.
  - `EEPROM.h`: For persistent storage.
  - `bsec.h`: For BME680 sensor data processing.

## Integration Points
- **WiFi**: Ensure the SSID and password are correctly configured in the `ssid` and `password` variables.
- **MQTT**: Update `mqtt_server`, `mqtt_port`, and `mqtt_topic` as needed for your MQTT broker.
- **LED Pins**: Modify `LED_OK` and `LED_ERROR` pin definitions if using different GPIOs.

## Example Patterns
### WiFi Connection
```cpp
if (connectWiFi()) {
  Serial.println("WiFi connected");
} else {
  Serial.println("WiFi connection failed");
}
```

### MQTT Publish
```cpp
if (client.publish(mqtt_topic, payload)) {
  Serial.println("Message published");
} else {
  Serial.println("Publish failed");
}
```

## Notes
- Ensure the BSEC library is correctly installed and configured for the BME680 sensor.
- Use a stable power supply to avoid unexpected resets during WiFi or MQTT operations.

---
Feel free to update this document as the project evolves.