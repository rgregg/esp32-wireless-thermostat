# TODO

- [x] When mode is set to Off, hide the setpoint (+/-) buttons on the display UI
- [x] Add filter change indicator on the home screen below indoor humidity

## Issue #22 — Separate setpoints for heat and cool modes

### Goal
Persist independent heat and cool setpoints on the controller so that switching modes restores the previously-used setpoint for that mode (instead of carrying over the other mode's value).

### Design summary
- **Source of truth: controller.** Add `heat_setpoint_c_` and `cool_setpoint_c_` to `ControllerRuntime` (defaults 20°C heat, 24°C cool). `target_temperature_c()` returns the bin matching `mode_` (Off → last-used bin; doesn't affect HVAC).
- **Routing on command:** `apply_remote_command` stores `cmd.setpoint` into the bin matching `cmd.mode` (Heat → heat bin; Cool → cool bin; Off → leave both alone but still apply mode change). Any sender — display, MQTT, HA — that sends `(mode, setpoint)` together always targets the matching bin.
- **Wire format:** No change. `CommandWord` and `ControllerTelemetryPacket` still carry one setpoint (the active one). Protocol version unchanged.
- **Display side:** Track `local_heat_setpoint_c_` and `local_cool_setpoint_c_` in `ThermostatApp`. When user switches mode, display sends the bin matching the new mode (so cool→heat doesn't overwrite the controller's stored cool bin). Both bins seed from telemetry into the bin matching telemetry's mode_code, so the display learns each bin the first time the controller reports that mode.
- **Persistence (controller NVS):** New keys `hvac_heat_sp_c` and `hvac_cool_sp_c`. On boot, if new keys missing, seed both from legacy `hvac_sp_c` (one-time migration).
- **MQTT/HA:** Keep single `cmd/target_temp_c` endpoint. The setpoint targets the *current mode's* bin via the normal CommandWord path. `state/target_temp_c` publishes the active bin. No discovery payload changes needed.

### Tasks
- [x] Add `heat_setpoint_c_` / `cool_setpoint_c_` to `ControllerRuntime`; update `target_temperature_c()`; route `apply_remote_command` setpoint into bin by `cmd.mode`.
- [x] Unit tests in `test_controller_runtime.cpp` for heat→cool→heat preserving each bin; mode=Off leaves bins unchanged.
- [x] Update `ThermostatApp` to track per-mode bins; selector based on `local_mode_`; sync telemetry into matching bin.
- [x] Unit tests in `test_thermostat_app.cpp` for display-side mode switching restoring previous bin.
- [x] Update controller firmware NVS persistence and restore-on-boot to two keys (`hvac_heat_sp_c`, `hvac_cool_sp_c`); legacy migration from `hvac_sp_c`.
- [x] Update MQTT mode/fan command handlers to refresh shadow setpoint from the runtime's matching bin before sending (so a mode change doesn't clobber the new mode's stored bin).
- [x] Sim: no changes needed — already uses `rt.target_temperature_c()`.
- [x] `pio run -e native-tests` green (99 tests pass, 4 new).
- [x] `pio run -e esp32-furnace-controller` green.
- [x] `pio run -e esp32-furnace-thermostat` green.

### Review
- **ControllerRuntime** (`include/controller/controller_runtime.h`, `src/controller/controller_runtime.cpp`): replaced single `target_temperature_c_` with `heat_setpoint_c_` (default 20°C) and `cool_setpoint_c_` (default 24°C). `target_temperature_c()` now returns the bin matching `mode_` (Cool→cool, Heat/Off→heat). `apply_remote_command` routes the incoming setpoint into the bin matching `cmd.mode`; for `cmd.mode == Off` both bins are preserved.
- **ThermostatApp** (`include/thermostat/thermostat_app.h`, `src/thermostat/thermostat_app.cpp`): mirrored the per-mode model on the display side. `set_local_setpoint_c` writes into the bin for the active mode. Telemetry/state callbacks route incoming setpoint into the bin matching the telemetry's mode (so each bin learns its value when the controller next reports that mode). `send_command` uses `local_setpoint_c()` (active bin).
- **Controller firmware** (`src/esp32_controller_main.cpp`): NVS keys split into `hvac_heat_sp_c` and `hvac_cool_sp_c` with one-time migration seeded from legacy `hvac_sp_c`. `cmd/mode` and `cmd/fan_mode` MQTT handlers now refresh the shadow setpoint from the runtime's matching bin before sending, so MQTT-initiated mode changes preserve the destination mode's stored bin. `cmd/target_temp_c` still flows the new value through unchanged (it correctly targets the active mode's bin via the existing CommandWord pathway).
- **Wire format**: unchanged. Protocol version unchanged. Single setpoint field on the wire is interpreted as "setpoint for the mode in this packet."
- **Tests added**: `controller_runtime_per_mode_setpoints_preserved_across_mode_switch`, `controller_runtime_off_mode_does_not_overwrite_setpoints`, `thermostat_app_per_mode_setpoints_independent`, `thermostat_app_telemetry_routes_setpoint_into_mode_bin`.

### Manual verification (recommended on hardware)
- Set heat to 21°C, switch to cool, set 23°C, switch back to heat → expect 21°C; switch to cool → expect 23°C.
- Reboot controller → both bins should restore from NVS.
- Existing devices with only `hvac_sp_c` in NVS should migrate to seed both bins on first boot.

### Follow-up: protocol-level "preserve" flags (forward-compatible infrastructure)

The bundled `CommandWord` previously forced every sender to assert mode + fan + setpoint together. That coupling was the reason this feature couldn't be a controller-only fix and why every future "settings semantics" change would require flashing both devices.

To unblock controller-only updates in the future, the protocol now carries three independent "preserve" flags (bits 24/25/26 of the packed command). A sender sets the flag for any field it does NOT intend to change; the controller, on decode, leaves that field's stored state untouched. The mode/fan/setpoint payload bits are still populated with the sender's current view — older controllers that don't understand the flags fall back to the previous full-state behavior, so this is backward-compatible without a protocol version bump.

**Sender semantics now:**
- Display `set_local_mode` → `preserve_fan=true`, `preserve_setpoint=true`
- Display `set_local_fan_mode` → `preserve_mode=true`, `preserve_setpoint=true`
- Display `set_local_setpoint_c` → `preserve_mode=true`, `preserve_fan=true`
- Display `request_filter_reset` → preserve all
- Controller MQTT `cmd/mode` / `cmd/fan_mode` / `cmd/target_temp_c` → preserve the unaffected fields (mirrors display semantics)
- Controller MQTT `cmd/filter_reset` → preserve all

**Controller routing:** if `preserve_setpoint=false`, the new setpoint is routed into the bin matching the *resolved* mode (i.e. the mode after preserve_mode handling), so a setpoint-only update from a display whose view of mode disagrees with the controller still lands in the controller-authoritative bin.

**Codec:** `codec_preserve_flags_round_trip` test verifies bits round-trip and default is false.
