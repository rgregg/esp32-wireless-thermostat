# System Spec And Parity Matrix

This document is the formal baseline spec for the thermostat/display + controller system, derived from the ESPHome YAML configuration, with a parity check against the current PlatformIO implementation.

## Scope

Baseline sources:
- `/Users/ryan/github/rgregg/esphome-sheridan/furnace-controller.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/furnace-display.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/lvgl-ux/ux-thermostat.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/esp32-8048S043C.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/templates/management-defaults.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/espnow/espnow-controller.yaml`
- `/Users/ryan/github/rgregg/esphome-sheridan/espnow/espnow-thermostat.yaml`

Port under evaluation:
- `/Users/ryan/github/rgregg/esp32-wireless-thermostat`

Status legend:
- `Implemented`: behavior is present and wired in current firmware.
- `Partial`: some behavior exists, but parity or wiring is incomplete.
- `Missing`: behavior is not implemented in the current port.

## Controller Spec And Parity

| ID | Requirement (from ESPHome behavior) | Status | Current implementation notes |
|---|---|---|---|
| C-01 | Accept thermostat command word and reject stale/duplicate sequence values. | Implemented | `ControllerRuntime::apply_remote_command` enforces sequence freshness (`espnow_cmd::is_newer_seq`). |
| C-02 | Track heartbeat freshness and enter failsafe after timeout (`failsafe_timeout_ms`). | Implemented | Runtime failsafe timeout implemented in `ControllerRuntime::update_failsafe`. |
| C-03 | In failsafe or lockout, all demands must be forced off. | Implemented | Runtime clears heat/cool/fan in `apply_hvac_calls` and `enforce_safety_interlocks`. |
| C-04 | Thermostat control loop must enforce min run/off/idle timing. | Implemented | `ControllerConfig` now includes min run/off/idle; state machine enforced in `ControllerRuntime`. |
| C-05 | Thermostat call logic should use deadband + overrun style behavior. | Implemented | `ControllerApp::compute_hvac_calls` uses configurable `heat_deadband/overrun` and `cool_deadband/overrun`. |
| C-06 | Fan modes: `automatic`, `always on`, `circulate`. | Implemented | Fan mode encoded/decoded and applied in runtime/app logic. |
| C-07 | Circulate scheduler should run windowed duty cycle based on period/duration minutes. | Implemented | Minute scheduler exists in `ControllerRuntime::run_minute_tasks`. |
| C-08 | Filter runtime increments while system is actively running and supports reset. | Partial | Runtime and reset are implemented; `spare_relay_4` participation is logical only (no physical output layer yet). |
| C-09 | Publish controller telemetry (`state`, `filter_runtime`, `lockout`, `mode`, `fan`, `setpoint`). | Implemented | `ControllerApp::publish` and `EspNowControllerTransport::publish_telemetry`. |
| C-10 | Relay demands must drive physical outputs on GPIO32/33/25/26 with startup-safe defaults. | Missing | No GPIO output layer yet for controller role; runtime state is not mapped to physical relay pins. |
| C-11 | Enforce relay interlock behavior equivalent to ESPHome relay interlock group. | Partial | Mutual exclusion is enforced logically; no explicit timed interlock delay equivalent to ESPHome `interlock_wait_time`. |
| C-12 | Local lockout entity should remain externally controllable and reflected in telemetry/state text. | Partial | Lockout behavior exists in runtime; no Home Assistant/API entity equivalent on controller firmware yet. |
| C-13 | Preserve last-known indoor temp/humidity fallback behavior when remote values are missing. | Missing | Controller currently uses latest received values only; no persisted fallback store like ESPHome template numbers. |

## Display/Thermostat Spec And Parity

| ID | Requirement (from ESPHome behavior) | Status | Current implementation notes |
|---|---|---|---|
| D-01 | 800x480 RGB panel on 8048S043C pin/timing map. | Implemented | RGB panel config/pins/timings set in `esp32s3_thermostat_firmware.cpp`. |
| D-02 | GT911 touch input on internal I2C bus. | Implemented | GT911 read/write + LVGL indev callback wired. |
| D-03 | AHT20 temperature/humidity sensor on external I2C bus with 60s polling. | Implemented | AHT20 initialized on external I2C and polled every 60s. |
| D-04 | Local temperature compensation offset before publishing indoor temp. | Missing | ESPHome `local_temperature_compensation_c` behavior is not implemented in current firmware. |
| D-05 | Local mode/fan/setpoint changes publish command word to controller. | Implemented | UI callbacks call runtime/app command publish path. |
| D-06 | Ignore remote setpoint updates for ~5s after local interaction (debounce). | Implemented | `ThermostatApp` enforces local interaction debounce window. |
| D-07 | Temperature unit preference (`fahrenheit`/`celsius`) drives conversions and UI format. | Implemented | `DisplayModel` and runtime unit handling implemented. |
| D-08 | Receive controller telemetry and map to user-visible status/mode/fan/setpoint. | Implemented | `ThermostatNode` + `ThermostatApp` + `ThermostatDisplayApp` flow implemented. |
| D-09 | LVGL pages and interactions for Home/Fan/Mode/Settings/Screensaver. | Partial | Core pages and actions implemented; UI is functional but not a 1:1 parity copy of all YAML widgets/details. |
| D-10 | Screen idle behavior: dim backlight, switch to screensaver, resume on touch. | Implemented | `ThermostatScreenController` + backlight dim/restore in firmware loop. |
| D-11 | Trigger sync/filter-reset actions from Settings UI. | Implemented | UI buttons call `request_sync` and `request_filter_reset`. |
| D-12 | Weather/outdoor data should come from Home Assistant entities. | Partial | Current firmware uses static outdoor stub (`Cloudy`, `6.0C`) unless external integration updates it. |
| D-13 | Display diagnostics in settings (IP/MAC/SSID/channel/RSSI/firmware). | Missing | These YAML diagnostics are not yet fully represented in current LVGL settings page. |
| D-14 | On boot, request/refresh state from controller. | Partial | Runtime starts transport and receives telemetry; explicit boot-time `get_data_from_controller` equivalent is limited. |

## Transport And Integration Spec And Parity

| ID | Requirement (from ESPHome behavior) | Status | Current implementation notes |
|---|---|---|---|
| T-01 | ESP-NOW heartbeat ping/pong between controller and display. | Implemented | Both transport adapters send/consume heartbeat packets. |
| T-02 | Command word bit contract must remain stable (mode/fan/setpoint/seq/sync/filter_reset). | Implemented | Codec in `espnow_cmd_word.*` preserved and test-covered. |
| T-03 | Indoor temperature and humidity packets sent display -> controller. | Implemented | Thermostat transport publishes float-value packets. |
| T-04 | Controller telemetry packets sent controller -> display. | Implemented | Controller transport sends telemetry packet structure. |
| T-05 | Peer/channel/encryption settings should match deployed devices. | Partial | Config fields exist, but defaults are zero peer MAC until user/site config sets real values. |

## Home Assistant/Management Spec And Parity

| ID | Requirement (from ESPHome behavior) | Status | Current implementation notes |
|---|---|---|---|
| H-01 | Expose thermostat control/status to Home Assistant. | Implemented | MQTT HA discovery + climate command/state topics added in thermostat firmware. |
| H-02 | Provide local fallback management interfaces (API/web/captive portal/OTA) similar to ESPHome templates. | Missing | Current PlatformIO port does not include ESPHome API/web server/captive portal stack. |
| H-03 | Keep credentials/secrets out of repo. | Implemented | `platformio_override.ini` is ignored; example file added for local credential overrides. |

## Validation Spec

| ID | Requirement | Status | Current implementation notes |
|---|---|---|---|
| V-01 | Native tests validate codec, controller runtime/app, thermostat app, display model, screen logic. | Implemented | `native-tests` currently runs 15 tests and passes. |
| V-02 | Build matrix must pass for host + controller + display targets. | Implemented | `native`, `native-tests`, `esp32dev-controller`, `esp32s3-display` pass. |
| V-03 | Hardware-in-loop verification for controller relays/display touch/panel/sensors. | Missing | Not yet automated or fully documented; pending physical validation. |

## Parity Summary

- `Implemented`: core safety state machine, command protocol, transport path, display runtime, and HA MQTT bridge.
- `Partial`: a set of parity items are functionally present but not 1:1 with ESPHome behavior.
- `Missing`: key gaps are physical controller relay GPIO integration, ESPHome-style management stack parity, and some display diagnostics/HA weather parity.

## Highest-Priority Remaining Gaps

1. Implement controller physical relay GPIO output layer with safe boot defaults and timed interlock behavior.
2. Replace display weather stub with real HA-fed or MQTT-fed weather/outdoor integration source.
3. Close remaining LVGL settings/diagnostic parity items (IP/MAC/SSID/channel/RSSI/firmware surface).
4. Add hardware validation checklist and pass/fail log for both controller and display boards.
