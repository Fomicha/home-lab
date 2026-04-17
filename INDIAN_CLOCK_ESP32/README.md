# ESP32 LED Clock with MAX7219 (Prototype)

> **This is the prototype version** built on an ESP32 DevKit development board for testing and iteration. The final production version using the compact ESP32-C3 Mini is in [`INDIAN_CLOCK_ESP32_MINI`](../INDIAN_CLOCK_ESP32_MINI/).

A digital clock based on ESP32 with a 4-module MAX7219 LED dot-matrix display, DHT11 temperature/humidity sensor, and automatic brightness adjustment via LDR photoresistor.

## Features

- **NTP Clock** -- time synchronization via WiFi
- **Temperature & Humidity** -- DHT11 sensor readings displayed in rotation
- **Auto Brightness** -- LDR photoresistor adjusts display based on ambient light
- **Night Sleep Mode** -- turns off display and WiFi between 00:00--08:00
- **Animated Transitions** -- scroll, mesh, and blinds effects between screens
- **Low Power** -- brightness capped at level 1 for battery operation

## Pinout (ESP32 DevKit)

| Component | Pin |
|-----------|-----|
| MAX7219 CLK | GPIO 18 |
| MAX7219 DATA | GPIO 23 |
| MAX7219 CS | GPIO 15 |
| DHT11 DATA | GPIO 4 |
| LDR | GPIO 34 (ADC) |

## Libraries

- `MD_Parola` / `MD_MAX72XX` -- LED matrix driver & animations
- `DHT sensor library` (Adafruit)
- `WiFi.h`, `time.h`, `SPI.h` -- built-in with ESP32 core

## Configuration

WiFi credentials loaded from shared config:

```cpp
#include "../../config/secrets.h"
```

### Timezone

```cpp
const int TIMEZONE_IN_SECONDS = 3600; // UTC+1 (Portugal summer time)
```

### Sleep Schedule

```cpp
#define NIGHT_HOUR_START 0   // sleep at 00:00
#define NIGHT_HOUR_END   8   // wake at 08:00
```

## What Changed in the Mini Version

The [ESP32-C3 Mini version](../INDIAN_CLOCK_ESP32_MINI/) adds:

- DHT22 sensor (0.1C resolution vs 1C on DHT11)
- MQTT integration with Home Assistant auto-discovery
- OTA firmware updates over WiFi
- KY-018 photoresistor module (replaces bare LDR)
- Proper timezone handling with DST (WET/WEST)
- Built-in LED as status indicator
- 3D-printed case

## License

MIT
