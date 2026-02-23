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
- `docs/`: system specification and implementation docs.
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
- Desktop controller preview build:
  - `pio run -e native-controller-preview`
- ESP32 controller firmware:
  - `pio run -e esp32-furnace-controller`
- ESP32-S3 display firmware:
  - `pio run -e esp32-furnace-thermostat`

## UI Preview (No Hardware)
- Build and run the thermostat display preview:
  - `pio run -e native-ui-preview`
  - `./.pio/build/native-ui-preview/program`
- Build and run the controller web UI preview:
  - `pio run -e native-controller-preview`
  - `./.pio/build/native-controller-preview/program`
- Capture thermostat page screenshots (home/fan/mode/settings/screensaver):
  - `./.pio/build/native-ui-preview/program --capture-dir .tmp/ui-capture`
- Requirements:
  - SDL2 development libraries available via `pkg-config` (example on macOS: `brew install sdl2`)
- Notes:
  - The thermostat preview mirrors screen layout and interactions for UI iteration.
  - Keyboard controls: `S` forces screensaver activation (via shared idle timeout logic), `W` wakes the screen.
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
- 37 native runtime tests passing for codec, controller runtime/app, thermostat app, display model, and relay IO behavior.
- CI workflow runs on push and pull requests.

## MQTT + Home Assistant
- Thermostat firmware publishes/subscribes MQTT topics and sends Home Assistant MQTT discovery for a climate entity.
- Wi-Fi provisioning behavior:
  - If `THERMOSTAT_WIFI_SSID` is set, firmware uses that static SSID/password.
  - If `THERMOSTAT_WIFI_SSID` is empty, firmware attempts stored credentials, then starts BLE provisioning (`WiFiProv`) after a short timeout.
- Network, MQTT, and device settings are configurable at runtime via the web config UI (`http://<device-ip>/config`) or MQTT config topics. Build flags in `platformio.ini` / `platformio_override.ini` set initial defaults. See `docs/deployment-runbook.md` for the full runtime config contract.
- Command topics:
  - `<base>/cmd/mode` (`off|heat|cool`)
  - `<base>/cmd/fan_mode` (`auto|on|circulate`)
  - `<base>/cmd/target_temp_c` (float C)
  - `<base>/cmd/unit` (`c|f`)
  - `<base>/cmd/sync` (`1|true|on`)
  - `<base>/cmd/filter_reset` (`1|true|on`)

## OTA Updates
- **ArduinoOTA**: network OTA via PlatformIO or `espota` (no serial required after initial flash).
- **Web OTA**: browser-based firmware upload at `http://<device-ip>/update` for both controller and display.
- **Automatic rollback**: new firmware boots in pending-verify state; WiFi + MQTT connectivity within 3 minutes confirms the update. Crash or timeout triggers automatic revert to the previous firmware.
- **GitHub Releases**: the release workflow automatically attaches firmware binaries to tagged releases.
