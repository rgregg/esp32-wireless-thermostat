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
