# esp32-wireless-thermostat

PlatformIO port of the ESPHome furnace controller + thermostat display project.

## What Is Ported
- Controller domain and safety logic:
  - failsafe timeout and lockout handling
  - relay interlock behavior
  - fan circulate scheduler
  - filter runtime accounting
- Thermostat command/telemetry protocol:
  - packed command-word codec
  - sequence/stale rejection semantics
- ESP-NOW transport adapters:
  - controller-side adapter
  - thermostat-side adapter
  - heartbeat, command, telemetry, and indoor climate packets
- Application wiring:
  - `ControllerNode` + `ControllerApp`
  - `ThermostatNode` + `ThermostatApp`
  - `ThermostatDisplayApp` + `DisplayModel` for UI-facing state and formatting
- Status/message mapping helpers equivalent to ESPHome state labels.

## Repository Layout
- `docs/`: migration audit and implementation docs.
- `include/`: public headers for runtime, transport, and app layers.
- `src/`: implementation code and native tests.

## Build
- Host compile:
  - `pio run -e native`
- Host test build:
  - `pio run -e native-tests`
- ESP32 controller firmware:
  - `pio run -e esp32dev-controller`
- ESP32-S3 display firmware:
  - `pio run -e esp32s3-display`

## Run Tests
- `./.pio/build/native-tests/program`

## Current Validation Status
- `native`, `native-tests`, `esp32dev-controller`, and `esp32s3-display` builds passing.
- Native runtime tests passing for codec, controller runtime/app, thermostat app, and display-model behavior.

## MQTT + Home Assistant
- Thermostat firmware now publishes/subscribes MQTT topics and sends Home Assistant MQTT discovery for a climate entity.
- Configure credentials and topic settings with build flags in `platformio.ini` or `platformio_override.ini`:
  - `THERMOSTAT_WIFI_SSID`
  - `THERMOSTAT_WIFI_PASSWORD`
  - `THERMOSTAT_MQTT_HOST`
  - `THERMOSTAT_MQTT_PORT` (default `1883`)
  - `THERMOSTAT_MQTT_USER`
  - `THERMOSTAT_MQTT_PASSWORD`
  - `THERMOSTAT_MQTT_CLIENT_ID`
  - `THERMOSTAT_MQTT_NODE_ID`
  - `THERMOSTAT_MQTT_BASE_TOPIC` (default `thermostat/furnace-display`)
  - `THERMOSTAT_MQTT_DISCOVERY_PREFIX` (default `homeassistant`)
- Command topics:
  - `<base>/cmd/mode` (`off|heat|cool`)
  - `<base>/cmd/fan_mode` (`auto|on|circulate`)
  - `<base>/cmd/target_temp_c` (float C)
  - `<base>/cmd/unit` (`c|f`)
  - `<base>/cmd/sync` (`1|true|on`)
  - `<base>/cmd/filter_reset` (`1|true|on`)
