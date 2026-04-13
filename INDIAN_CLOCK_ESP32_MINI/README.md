# ESP32-C3 Mini LED Clock with DHT22 & Home Assistant

A smart LED dot-matrix clock built on ESP32-C3 Mini with a 4-module MAX7219 display, DHT22 temperature/humidity sensor, KY-018 photoresistor for auto-brightness, and full Home Assistant integration via MQTT.

Designed to fit inside a 3D-printed transparent PETG case.

**3D Model:** [MakerWorld link here]

## Features

- **NTP Clock** -- auto-synced time with 7-segment font, flashing colon separator
- **DHT22 Sensor** -- temperature and humidity displayed in rotation with the clock
- **Automatic Brightness** -- LDR photoresistor adjusts display brightness based on ambient light
- **Night Sleep Mode** -- display and WiFi turn off between 00:00--08:00, wake up automatically
- **MQTT to Home Assistant** -- auto-discovery, publishes temperature, humidity, and WiFi RSSI every 2 minutes
- **OTA Updates** -- firmware updates over WiFi (no USB needed after first flash)
- **Built-in LED** -- blue LED on ESP32-C3 glows through transparent case
- **Animated Transitions** -- scroll, mesh, blinds, and grow effects between clock/date/temp/humidity screens
- **Boot Loader Animation** -- dot animation and status messages during WiFi/NTP connection
- **Portugal Timezone** -- WET/WEST with automatic DST switching (last Sunday of March/October)

## Display Cycle

| Phase | Content | Duration |
|-------|---------|----------|
| 1 | Clock (HH:MM) | 60 seconds |
| 2 | Day & date (e.g. "Mon 14") | ~3 seconds |
| 3 | Temperature (e.g. "T: 23 C") | ~3 seconds |
| 4 | Humidity (e.g. "H: 45 %") | ~3 seconds |

## Hardware

| Component | Pin |
|-----------|-----|
| MAX7219 CLK | GPIO 4 |
| MAX7219 DATA | GPIO 6 |
| MAX7219 CS | GPIO 7 |
| DHT22 DATA | GPIO 2 |
| KY-018 (photoresistor) | GPIO 0 (ADC) |
| Built-in LED | GPIO 8 |

### Components

- ESP32-C3 Mini (e.g. Lolin C3 Mini)
- 4x MAX7219 8x8 LED matrix modules (FC16 type)
- DHT22 temperature & humidity sensor
- KY-018 photoresistor module

## Home Assistant Integration

The clock registers itself as an MQTT device with three sensors:

| Entity | Description |
|--------|-------------|
| `Clock LR Temperature` | Room temperature (0.1C resolution) |
| `Clock LR Humidity` | Relative humidity |
| `Clock LR WiFi Signal` | WiFi RSSI in dBm |

MQTT topics:
- State: `home/living-room/clock-sensor/state`
- Availability: `home/living-room/clock-sensor/status`
- Device ID: `livingroom_clock_dht22`

Auto-discovery payloads are published to `homeassistant/sensor/...` on first connection.

## OTA (Over-The-Air Updates)

After the first USB flash, update wirelessly:

```bash
./ota-upload.sh clock
```

Hostname: `homelab-clock` (port 3232)

## Configuration

WiFi and MQTT credentials are loaded from a shared config file:

```
HOME_LAB/
  config/
    secrets.h        # your credentials (git-ignored)
    secrets.h.example # template
```

### Timezone

Default is Portugal (WET/WEST). Change in the sketch:

```cpp
const char* TIMEZONE_STRING = "WET0WEST,M3.5.0/1,M10.5.0";
```

### Sleep Schedule

```cpp
#define NIGHT_HOUR_START 0   // sleep starts at 00:00
#define NIGHT_HOUR_END   8   // wake up at 08:00
```

During sleep: display off, WiFi off, MQTT gracefully disconnected with "offline" status.

### Sensor Interval

```cpp
#define SENSOR_UPDATE_INTERVAL 120000UL  // 2 minutes
```

## Libraries

Install via Arduino IDE Library Manager:

- **MD_Parola** -- LED matrix text/animation
- **MD_MAX72XX** -- MAX7219 driver (installed with MD_Parola)
- **DHT sensor library** (Adafruit)
- **PubSubClient** -- MQTT client
- **ArduinoJson** (if needed for other sketches)
- **ArduinoOTA** (built-in with ESP32 core)

## Serial Debug

115200 baud. Log prefixes: `[WiFi]`, `[NTP]`, `[TEMP]`, `[HUM]`, `[SLEEP]`, `[MQTT]`, `[OTA]`, `[CLOCK_IP]`

## License

MIT
