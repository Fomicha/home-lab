# Home Lab

A collection of ESP32-based Arduino projects for home monitoring, connected via MQTT and Home Assistant.

## Projects

| Project | Board | Description |
|---------|-------|-------------|
| [`home_lab_monitor_esp32_4inch`](home_lab_monitor_esp32_4inch/) | ESP32 | 480x320 TFT dashboard — Proxmox stats, weather, room sensors, system panel |
| [`INDIAN_CLOCK_ESP32_MINI`](INDIAN_CLOCK_ESP32_MINI/) | ESP32-C3 Mini | LED dot-matrix clock with DHT22, MQTT to HA, auto-brightness, OTA |
| [`INDIAN_CLOCK_ESP32`](INDIAN_CLOCK_ESP32/) | ESP32 DevKit | Clock prototype (predecessor to the Mini version) |
| [`MQTT_BME-680-ESP32WROOm`](MQTT_BME-680-ESP32WROOm/) | ESP32-WROOM | BME680 air quality sensor — temperature, humidity, IAQ, CO2, pressure |
| [`MQTT_ESP32C3_DHT22_Baby-room`](MQTT_ESP32C3_DHT22_Baby-room/) | ESP32-C3 Mini | Baby room DHT22 temperature & humidity sensor |

## Architecture

All sensor devices publish data to Home Assistant via MQTT auto-discovery. The monitor fetches data from HA REST API, Netdata (Proxmox), and OpenWeatherMap.

```
Sensors (MQTT) --> Home Assistant --> Monitor (REST API)
                                         |
                   Netdata (Proxmox) -----+
                   OpenWeatherMap --------+
```

## Shared Config

WiFi, MQTT, and API credentials are centralized in `config/secrets.h` (git-ignored). See [`config/secrets.h.example`](config/secrets.h.example) for the template.

## OTA Updates

Both the monitor and clock support over-the-air firmware updates:

```bash
./ota-upload.sh monitor   # 192.168.1.251
./ota-upload.sh clock     # 192.168.1.232
```

## License

MIT
