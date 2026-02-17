# ESPHome Furnace YAML Audit

This audit covers the furnace thermostat set of YAMLs:

- `/Users/ryan/github/rgregg/esphome-sheridan/furnace-controller.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/furnace-display.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/test-furnace-display.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/espnow/espnow-controller.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/espnow/espnow-thermostat.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/lvgl-ux/ux-thermostat.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/esp32-8048S043C.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/management-defaults.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/sntp-clock.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/firmware-version.yaml`

## Functional Inventory

## 1) Furnace Controller Device

Primary responsibilities:
- Accept remote thermostat measurements and user commands over ESP-NOW packet transport.
- Drive relay outputs with interlock logic:
  - `heat_demand` GPIO32
  - `cool_demand` GPIO33
  - `fan_demand` GPIO25
  - `spare_relay_4` GPIO26
- Enforce lockout and failsafe behavior.
- Run thermostat climate logic and track filter runtime.

Key behavior to preserve:
- Failsafe triggers if heartbeat timeout exceeds `failsafe_timeout_ms` (default 300000 ms).
- In failsafe or lockout, all relay activation attempts are blocked and relays are forced off.
- Furnace state code mapping:
  - 0 Idle
  - 1 Heat mode
  - 2 Heat on
  - 3 Cool mode
  - 4 Cool on
  - 5 Fan on
  - 6 Error/failsafe/lockout
- Fan modes:
  - `automatic`
  - `always on`
  - `circulate`
- Circulate mode scheduler uses period/duration (minutes) with a rolling elapsed counter.
- Filter runtime increments while any relay demand is active.

## 2) Display/Thermostat Device

Primary responsibilities:
- Read local AHT20 temperature and humidity.
- Present LVGL UI on RGB panel + GT911 touch.
- Allow local user changes to mode/fan/setpoint and push to controller.
- Receive controller state and render a friendly status.

Key behavior to preserve:
- Display-level setpoint debounce: ignore remote setpoint updates for 5 s after local interaction.
- Temperature unit preference (F/C) affects UI conversions and command packing.
- Screen idle behavior:
  - dim backlight
  - switch to screensaver page
  - resume on touch
- Weather and outdoor temperature integration via Home Assistant entities.

## 3) Controller <-> Display Transport Contract

Transport pattern:
- ESP-NOW with encrypted packet transport and heartbeat ping/pong.
- Both devices maintain `*_connected` status from heartbeat freshness.

Outgoing controller telemetry to display:
- `furnace_state`
- `filter_runtime_hours`
- `furnace_lockout`
- `furnace_mode`
- `fan_mode`
- `temperature_setpoint_c`

Outgoing display telemetry to controller:
- `cmd_set_values` (packed command word)
- `indoor_temperature_c`
- `indoor_humidity`

Command word layout (24 bits sent via float payload):
- bits 0-1: mode (0 off, 1 heat, 2 cool)
- bits 2-3: fan (0 auto, 1 always on, 2 circulate)
- bits 4-12: setpoint in deci-C (0..400)
- bits 13-21: sequence (9-bit wrap)
- bit 22: filter reset toggle
- bit 23: sync request

Stale command rejection:
- 9-bit sequence ring comparison; reject duplicate and stale packets.

## 4) Shared/Template Capabilities

- Management defaults provide API, captive portal, web OTA, web server auth.
- SNTP clock drives UI clock updates.
- Firmware version is surfaced via text sensor.

## Migration Risks and Notes

- ESPHome climate thermostat behavior must be replaced by explicit state machine logic in C++.
- Existing implementation sends packed command as float; preserve exact bit behavior to avoid interop regressions.
- Relay interlock and startup defaults are safety-critical and should be treated as blocking requirements.
- LVGL UX parity is now implemented with a C++ LVGL firmware layer; remaining validation is hardware-in-loop behavior and visual polish checks.
