#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <broker-host> <base-topic-prefix>"
  echo "example: $0 192.168.1.10 thermostat"
  exit 1
fi

BROKER="$1"
PREFIX="$2"
CTRL_BASE="${PREFIX}/furnace-controller"
DISP_BASE="${PREFIX}/furnace-display"

echo "== Build checks =="
pio run -e native-tests >/dev/null
./.pio/build/native-tests/program >/dev/null
pio run -e esp32-furnace-controller >/dev/null
pio run -e esp32-furnace-thermostat >/dev/null
echo "Build/test checks passed"

echo "== MQTT lockout smoke =="
mosquitto_pub -h "$BROKER" -t "${CTRL_BASE}/cmd/lockout" -m "1"
sleep 1
mosquitto_pub -h "$BROKER" -t "${CTRL_BASE}/cmd/lockout" -m "0"
echo "Lockout command smoke complete"

echo "== Display command smoke =="
mosquitto_pub -h "$BROKER" -t "${DISP_BASE}/cmd/mode" -m "heat"
mosquitto_pub -h "$BROKER" -t "${DISP_BASE}/cmd/fan_mode" -m "circulate"
mosquitto_pub -h "$BROKER" -t "${DISP_BASE}/cmd/target_temp_c" -m "21.5"
mosquitto_pub -h "$BROKER" -t "${DISP_BASE}/cmd/display_timeout_s" -m "180"
echo "Display command smoke complete"

echo "== Outdoor/weather feed smoke =="
if [[ -n "${OUTDOOR_TEMP_TOPIC:-}" ]]; then
  mosquitto_pub -h "$BROKER" -t "$OUTDOOR_TEMP_TOPIC" -m "4.0"
fi
if [[ -n "${WEATHER_TOPIC:-}" ]]; then
  mosquitto_pub -h "$BROKER" -t "$WEATHER_TOPIC" -m "Cloudy"
fi
echo "HIL smoke script complete"
