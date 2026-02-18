# PlatformIO Port Plan

## Verification Scope
Task list verified against:
- `../esphome-sheridan/furnace-controller.yaml`
- `../esphome-sheridan/furnace-display.yaml`
- `../esphome-sheridan/espnow/espnow-controller.yaml`
- `../esphome-sheridan/espnow/espnow-thermostat.yaml`
- `../esphome-sheridan/lvgl-ux/ux-thermostat.yaml`
- `../esphome-sheridan/templates/management-defaults.yaml`
- `../esphome-sheridan/templates/sntp-clock.yaml`

## Completed
- Controller/runtime core: command decode + sequence freshness, failsafe + lockout gating, min run/off/idle state machine, fan circulate scheduler, filter runtime accounting.
- Controller->thermostat update stream now carries versioned sequence IDs with thermostat ACK path and duplicate/stale rejection.
- Thermostat/runtime core: command publishing, local interaction debounce, telemetry ingestion, transport heartbeat connectivity.
- Thermostat boot-time sync request is now sent at transport startup.
- Thermostat local temperature compensation is implemented in runtime/app and applied before publishing indoor temperature.
- Outdoor weather/outdoor-temperature stub is now replaceable by real MQTT-fed topics (with stub only as fallback when feeds are absent).
- ESP-NOW transport on both roles: heartbeat, command word, controller telemetry, indoor temperature, indoor humidity.
- ESP32-S3 display bring-up: RGB panel, GT911 touch, AHT20 read loop, LVGL wiring, screensaver/backlight behavior.
- Controller relay GPIO output layer is implemented with safe defaults plus interlock-delay transitions (GPIO32/33/25/26).
- Controller indoor temperature/humidity fallback values are persisted (NVS-backed on device) and used when remote values are unavailable.
- Controller lockout now has an external MQTT control/state surface (`cmd/lockout`, `state/lockout`).
- Controller now supports MQTT command ingestion for mode/fan/setpoint/sync/filter-reset, with MQTT-primary authority window and ESP-NOW command fallback/failback.
- Controller conflict resolution is implemented by gating ESP-NOW commands while recent MQTT control traffic is active.
- Controller and thermostat MQTT discovery now share one logical Home Assistant device identity (`wireless_thermostat_system`).
- Thermostat settings/diagnostics MQTT coverage is expanded (display timeout, firmware version, WiFi diagnostics).
- Thermostat clock now uses SNTP/NTP time configuration and renders local time instead of uptime seconds.
- Runtime display timeout is now configurable (`cmd/display_timeout_s`, retained `state/display_timeout_s`).
- ESP-NOW deployment config path is implemented via build flags for peer MAC, channel, and LMK on both controller and thermostat.
- ESP-NOW send result diagnostics counters are implemented in both transport adapters.
- Hardware-in-loop checklist and smoke automation script are now included.
- Management stack decision is documented: MQTT/provisioning is the intentional replacement for ESPHome management stack parity.
- Core host and target builds/tests are passing.

## Remaining Work (P0)
- None.

## Remaining Work (P1)
- None.

## Remaining Work (P2 / Optional Hardening)
- None.

## Validation
Passing now:
- `pio run -e native`
- `pio run -e native-tests`
- `pio run -e esp32dev-controller`
- `pio run -e esp32s3-display`
- `./.pio/build/native-tests/program`

Native tests currently validate:
- command codec roundtrip + sequence handling
- controller failsafe/lockout and circulation/filter timing
- controller app auto HVAC behavior from indoor temperature
- thermostat app debounce and command publication
- display app/model formatting, conversion, weather mapping, and status text
