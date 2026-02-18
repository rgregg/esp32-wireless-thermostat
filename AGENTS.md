# AGENTS.md

## Project Scope
- Two-firmware ESP32 thermostat system:
  - `esp32-furnace-controller` (furnace controller)
  - `esp32-furnace-thermostat` (display/thermostat UI)
- Core responsibilities:
  - control/safety runtime logic
  - ESP-NOW device link
  - MQTT + Home Assistant integration
  - runtime config persistence (NVS)
  - OTA + web management endpoints

## Build + Test (Required Before Commit)
1. `pio run -e native-tests`
2. `./.pio/build/native-tests/program`
3. `pio run -e esp32-furnace-controller`
4. `pio run -e esp32-furnace-thermostat`

## Environment Names
- Host tests: `native-tests`
- Controller firmware: `esp32-furnace-controller`
- Thermostat firmware: `esp32-furnace-thermostat`

## Device Defaults
- MQTT host: `mqtt.lan`
- MQTT user/password: empty
- ESP-NOW channel: `6` (both devices)
- Controller MQTT client ID: `esp32-furnace-controller`
- Thermostat MQTT client ID: `esp32-furnace-thermostat`
- Controller OTA/mDNS hostname: `esp32-furnace-controller`
- Thermostat OTA/mDNS hostname: `esp32-furnace-thermostat`

## MQTT Topic Conventions
- Controller base: `thermostat/furnace-controller`
- Thermostat base: `thermostat/furnace-display`
- Commands: `<base>/cmd/...`
- State: `<base>/state/...`
- Runtime config set/state:
  - `<base>/cfg/<key>/set`
  - `<base>/cfg/<key>/state`

## Management Surfaces
- Controller unified UI: `http://<controller-ip>/`
  - edits controller local config
  - proxies display config via MQTT
- JSON config endpoint: `/config`
- Thermostat screenshot endpoint: `http://<display-ip>/screenshot`

## Versioning
- Firmware version is build-injected from:
  - `git describe --tags --long --dirty --always`
- Do not hardcode release versions in source.
- Build script: `scripts/git_version.py`

## Files Agents Should Know
- Controller firmware entry: `src/esp32_controller_main.cpp`
- Thermostat firmware entry: `src/esp32_thermostat_main.cpp`
- Topic/form parsing helpers: `include/management_paths.h`, `src/management_paths.cpp`
- Integration tests for routing helpers: `src/tests/test_management_paths.cpp`
- Deployment notes: `docs/deployment-runbook.md`

## Editing Guardrails
- Keep compile-time defaults as bootstrap only; runtime values must remain configurable/persisted.
- Preserve MQTT/ESP-NOW fallback behavior and sequence freshness logic.
- Keep Home Assistant discovery payloads backward-compatible when possible.
- Do not log or expose plaintext secrets in state endpoints/UI.
- Update docs when changing env names, topics, defaults, or management endpoints.
