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
- `enclosures/`: 3D printable CAD files for controller and thermostat housings.
- `include/`: public headers for runtime, transport, and app layers.
- `src/`: implementation code and native tests.

Primary specification and parity guide:
- `docs/system-spec-and-parity.md`
- `docs/hardware-requirements.md`

## Build
- Host compile:
  - `pio run -e native`
- Host test build:
  - `pio run -e native-tests`
- Desktop thermostat UI preview build:
  - `pio run -e native-ui-preview`
- ESP32 controller firmware:
  - `pio run -e esp32-furnace-controller`
- ESP32-S3 display firmware:
  - `pio run -e esp32-furnace-thermostat`

## UI Preview (No Hardware)
- Build:
  - `pio run -e native-ui-preview`
- Run:
  - `./.pio/build/native-ui-preview/program`
- Capture page screenshots (home/fan/mode/settings):
  - `./.pio/build/native-ui-preview/program --capture-dir .tmp/ui-capture`
- Verify UI screenshots against golden baselines:
  - `scripts/ui_golden_check.sh`
- Update golden baselines after intentional UI changes:
  - `scripts/ui_golden_check.sh --update`
- Requirements:
  - SDL2 development libraries available via `pkg-config` (example on macOS: `brew install sdl2`)
- Notes:
  - The preview mirrors thermostat screen layout and interactions for UI iteration.
  - Hardware-specific behavior (RGB timing, touch controller, ESP-NOW, sensor buses) is not simulated.

## UI Fonts
- Shared thermostat UI uses generated Montserrat assets from one source TTF (`third_party/fonts/Montserrat-Regular.ttf`).
- Sizes are parity-aligned with the ESPHome design: `20/26/28/30/40/48/80/120`.
- Regenerate fonts with:
  - `scripts/generate_ui_fonts.sh`

## Run Tests
- `./.pio/build/native-tests/program`

## Current Validation Status
- `native`, `native-tests`, `esp32-furnace-controller`, and `esp32-furnace-thermostat` builds passing.
- Native runtime tests passing for codec, controller runtime/app, thermostat app, and display-model behavior.

## MQTT + Home Assistant
- Thermostat firmware now publishes/subscribes MQTT topics and sends Home Assistant MQTT discovery for a climate entity.
- Both firmware roles support network OTA updates via `ArduinoOTA` (no serial required after install).
- Wi-Fi provisioning behavior:
  - If `THERMOSTAT_WIFI_SSID` is set, firmware uses that static SSID/password.
  - If `THERMOSTAT_WIFI_SSID` is empty, firmware attempts stored credentials, then starts BLE provisioning (`WiFiProv`) after a short timeout.
- Configure network and topic settings with build flags in `platformio.ini` or `platformio_override.ini`:
  - `THERMOSTAT_WIFI_SSID` (optional)
  - `THERMOSTAT_WIFI_PASSWORD` (optional; used with `THERMOSTAT_WIFI_SSID`)
  - `THERMOSTAT_MQTT_HOST`
  - `THERMOSTAT_MQTT_PORT` (default `1883`)
  - `THERMOSTAT_MQTT_USER`
  - `THERMOSTAT_MQTT_PASSWORD`
  - `THERMOSTAT_MQTT_CLIENT_ID`
  - `THERMOSTAT_MQTT_NODE_ID`
  - `THERMOSTAT_MQTT_BASE_TOPIC` (default `thermostat/furnace-display`)
  - `THERMOSTAT_MQTT_DISCOVERY_PREFIX` (default `homeassistant`)
  - `THERMOSTAT_PROV_POP` (default `thermostat-setup`)
  - `THERMOSTAT_PROV_SERVICE_NAME` (default `PROV_ESP32_THERMOSTAT`)
  - `THERMOSTAT_PROV_RESET_PROVISIONED` (`0` default, `1` to clear saved credentials at boot)
  - `THERMOSTAT_OTA_HOSTNAME` (display OTA hostname, default `furnace-display`)
  - `THERMOSTAT_OTA_PASSWORD` (optional display OTA password)
  - `THERMOSTAT_CONTROLLER_OTA_HOSTNAME` (controller OTA hostname, default `furnace-controller`)
  - `THERMOSTAT_CONTROLLER_OTA_PASSWORD` (optional controller OTA password)
- Command topics:
  - `<base>/cmd/mode` (`off|heat|cool`)
  - `<base>/cmd/fan_mode` (`auto|on|circulate`)
  - `<base>/cmd/target_temp_c` (float C)
  - `<base>/cmd/unit` (`c|f`)
  - `<base>/cmd/sync` (`1|true|on`)
  - `<base>/cmd/filter_reset` (`1|true|on`)
