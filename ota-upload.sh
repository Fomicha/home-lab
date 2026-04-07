#!/bin/bash
# OTA upload script for HOME_LAB devices
# Usage: ./ota-upload.sh monitor|clock [optional-ip-override]

set -euo pipefail

BASE="/Users/of/Documents/Arduino/HOME_LAB"
PORT="3232"
TIMEOUT="30"

detect_espota() {
  local base="$HOME/Library/Arduino15/packages/esp32/hardware/esp32"
  local best_path=""
  local best_major=-1
  local best_minor=-1
  local best_patch=-1
  local path
  local ver
  local major
  local minor
  local patch

  while IFS= read -r path; do
    ver="$(echo "$path" | sed -E 's#^.*/esp32/([0-9]+)\.([0-9]+)\.([0-9]+)/tools/espota.py$#\1 \2 \3#')"
    if [ "$ver" = "$path" ]; then
      continue
    fi

    read -r major minor patch <<< "$ver"
    if [ "$major" -gt "$best_major" ] ||
       { [ "$major" -eq "$best_major" ] && [ "$minor" -gt "$best_minor" ]; } ||
       { [ "$major" -eq "$best_major" ] && [ "$minor" -eq "$best_minor" ] && [ "$patch" -gt "$best_patch" ]; }; then
      best_major="$major"
      best_minor="$minor"
      best_patch="$patch"
      best_path="$path"
    fi
  done < <(find "$base" -type f -name "espota.py" 2>/dev/null)

  if [ -n "$best_path" ]; then
    echo "$best_path"
    return 0
  fi

  return 1
}

find_bin() {
  local sketch_dir="$1"
  local build_dir="$sketch_dir/build"
  if [ ! -d "$build_dir" ]; then
    return 1
  fi

  local newest_bin=""
  local newest_mtime=0
  local candidate
  local mtime

  while IFS= read -r candidate; do
    mtime="$(stat -f "%m" "$candidate" 2>/dev/null || stat -c "%Y" "$candidate" 2>/dev/null || echo 0)"
    if [ "$mtime" -gt "$newest_mtime" ]; then
      newest_mtime="$mtime"
      newest_bin="$candidate"
    fi
  done < <(find "$build_dir" -type f -name "*.ino.bin" ! -name "*.merged.bin")

  if [ -z "$newest_bin" ]; then
    return 1
  fi

  echo "$newest_bin"
}

detect_host_ip_for_target() {
  local target_ip="$1"
  local iface
  iface="$(route -n get "$target_ip" 2>/dev/null | awk '/interface:/{print $2; exit}')"
  if [ -z "$iface" ]; then
    return 1
  fi

  local host_ip
  host_ip="$(ipconfig getifaddr "$iface" 2>/dev/null || true)"
  if [ -z "$host_ip" ]; then
    return 1
  fi

  echo "$host_ip"
}

case "${1:-}" in
  monitor)
    TARGET="monitor"
    IP="192.168.1.251"
    SKETCH_DIR="$BASE/home_lab_monitor_esp32_4inch"
    ;;
  clock)
    TARGET="clock"
    IP="192.168.1.232"
    SKETCH_DIR="$BASE/INDIAN_CLOCK_ESP32_MINI/esp32-mini-max7219-clock"
    ;;
  *)
    echo "Usage: $0 monitor|clock [optional-ip-override]"
    echo ""
    echo "  monitor  - Home Lab Monitor (ESP32, 192.168.1.251)"
    echo "  clock    - LED Clock Mini (ESP32-C3, 192.168.1.232)"
    exit 1
    ;;
esac

if [ -n "${2:-}" ]; then
  IP="$2"
fi

if ! ESPOTA="$(detect_espota)"; then
  echo "ERROR: espota.py not found under: $HOME/Library/Arduino15/packages/esp32/hardware/esp32"
  exit 1
fi

if [ ! -f "$ESPOTA" ]; then
  echo "ERROR: espota.py not found: $ESPOTA"
  exit 1
fi

if ! BIN="$(find_bin "$SKETCH_DIR")"; then
  echo "ERROR: Compiled binary not found under: $SKETCH_DIR/build"
  echo "Build/export first (Arduino IDE): Sketch -> Export Compiled Binary"
  exit 1
fi

echo "Uploading to $TARGET ($IP)..."
echo "Binary: $BIN"
echo "espota: $ESPOTA"

HOST_IP=""
if HOST_IP="$(detect_host_ip_for_target "$IP")"; then
  echo "Host IP: $HOST_IP"
fi

if ! ping -c 1 "$IP" >/dev/null 2>&1; then
  echo "WARNING: Ping failed for $IP (device may still work for OTA, continuing...)"
fi

set +e
if [ -n "$HOST_IP" ]; then
  python3 "$ESPOTA" -i "$IP" -I "$HOST_IP" -p "$PORT" -t "$TIMEOUT" -f "$BIN"
else
  python3 "$ESPOTA" -i "$IP" -p "$PORT" -t "$TIMEOUT" -f "$BIN"
fi
rc=$?
set -e

if [ $rc -ne 0 ]; then
  echo ""
  echo "OTA upload failed. Quick checks:"
  echo "1) Device IP is correct (or pass override: ./ota-upload.sh $TARGET <ip>)"
  echo "2) Device is running firmware with ArduinoOTA.begin() enabled"
  echo "3) Device is on WiFi and same subnet as this Mac"
  echo "4) Reboot device once, then retry"
  exit $rc
fi

echo "OTA upload complete."
