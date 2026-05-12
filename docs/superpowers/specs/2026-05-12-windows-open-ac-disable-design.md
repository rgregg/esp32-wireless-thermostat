# Windows Open A/C Disable — Design

**Date:** 2026-05-12
**Status:** Approved — ready for implementation plan
**Scope:** Controller firmware, thermostat display firmware, sim

## Summary

Add a new boolean state, `windows_open`, owned by Home Assistant and pushed to the controller over MQTT. While the signal is active the controller suppresses cooling demand only — heat, fan, mode/setpoint changes, and all other HVAC behavior continue normally. The thermostat display surfaces a "Windows Open" message in the status line **only when the active mode is Cool**.

## Motivation

When a user opens windows/doors during cooling season, running the A/C is wasteful and uncomfortable. Home Assistant already aggregates door/window sensor state. This feature gives HA a clean, dedicated lever to disable cooling without forcing the user (or an automation) to toggle the whole HVAC mode off — which would also stop heating during shoulder seasons or unrelated cases.

## Design Decisions

| Question | Decision | Rationale |
|---|---|---|
| Source of signal | MQTT command from HA only | Mirrors existing `hvac_lockout` pattern; no controller hardware change. |
| Behavior during active cooling | Honor min-run-time, then idle | Avoids short-cycling compressor on brief door openings. |
| Scope of suppression | Cooling only | Per user request. Fan (circulate/always-on) and heat continue normally. |
| Display priority | Only show "Windows Open" when active mode is Cool | When in Heat/Off, the windows-open state is irrelevant to the user's current intent. |
| State persistence | None on controller | HA publishes the command as a retained MQTT topic; controller picks it up on reconnect. |
| Furnace state code | No new code | Suppressed-cooling-Idle is still functionally Idle; flag rides separately on snapshot. |

## Architecture

### Data flow

```
HA windows/doors aggregation
        │
        │ MQTT: {base}/devices/{ctrl_mac}/cmd/windows_open  (retained)
        ▼
Controller (ESP32)
  ControllerApp::set_windows_open(bool)
        │
        ▼
  ControllerRuntime::windows_open_  ──► suppresses cool_call in apply_hvac_calls()
        │
        │ Published in retained state JSON, included in ESP-NOW snapshot
        ▼
Thermostat Display
  parses windows_open into mirrored snapshot
        │
        ▼
  furnace_state_text() returns "Windows Open" when mode==Cool and flag set
```

### Components changed

**Shared (`include/`):**
- `thermostat_types.h` — `ThermostatSnapshot` gains `bool windows_open = false`.
- `espnow_cmd_word.h` — snapshot serializer gains one bit for `windows_open`. If no free bit exists in the current packed word, this becomes a protocol-version bump — both controller and display firmwares must be flashed in lockstep. Implementation plan must verify bit availability first.

**Controller (`src/controller/`, `include/controller/`):**
- `controller_runtime.h/.cpp`
  - New field `bool windows_open_ = false`.
  - New methods `void set_windows_open(bool)` and `bool windows_open() const`.
  - `apply_hvac_calls(now, heat_call, cool_call)`: at function entry, force `cool_call = false` if `windows_open_` is true. Existing min-run-time logic in the `Cooling` branch handles graceful shutdown.
  - `snapshot()` populates `s.windows_open`.
  - State transitions audit-logged: `"windows_open: on/off [mqtt]"`.
- `controller_app.h/.cpp` — thin pass-through `set_windows_open(bool)`.

**Controller firmware (`src/esp32_controller_main.cpp`):**
- Subscribe and handle new MQTT command topic `cmd/windows_open` (gated by `ha_allowed`, alongside `cmd/lockout`).
- Add `"windows_open": true/false` to retained runtime-state JSON.
- Add status-page HTML row "Windows Open".
- Publish HA MQTT discovery for a `switch` entity (mirroring `hvac_lockout` discovery) so HA can both drive and observe the value.

**Display (`src/thermostat/`, `include/thermostat/`):**
- `thermostat_ui_state.h/.cpp` — `furnace_state_text(...)` gains parameter `bool windows_open` and parameter `FurnaceMode mode` (or accepts the full snapshot — see plan). Returns `"Windows Open"` when `mode == FurnaceMode::Cool && windows_open` and no higher-priority state applies.
- MQTT subscriber on display parses `windows_open` from controller state JSON.
- ESP-NOW receiver path stores the bit into the mirrored snapshot.

**Sim (`src/sim/controller_preview.cpp`):**
- Toggle button for `windows_open`.
- Parse `cmd/windows_open` on the sim's MQTT bridge.
- Status overlay shows windows-open status (parity with lockout).

### Status text priority (display)

Updated order in `furnace_state_text`:

```
1. Failsafe        → "Failsafe"
2. Locked Out      → "Locked Out"
3. Not Connected   → "Not Connected"
4. mode==Cool && windows_open
                   → "Windows Open"
5. (existing state-code switch: Idle, HeatMode, HeatOn, CoolMode,
    CoolOn, FanOn, HeatWait, CoolWait, Error)
```

If `windows_open` is true but the user is in Heat or Off mode, the status reads as it would today (no "Windows Open" text). If a compressor is still finishing its min-run-time after `windows_open` goes true, the user briefly sees `"Cool on"`. Once cooling idles, the text transitions to `"Windows Open"`.

### Cooling suppression — exact semantics

In `ControllerRuntime::apply_hvac_calls(now_ms, heat_call, cool_call)`:

```cpp
if (windows_open_) {
  cool_call = false;
}
```

This goes **after** the existing `failsafe_active_ || hvac_lockout_` early-return (so those still force full idle) and **before** the state machine switch. Effect:

- **HvacState::Cooling**: existing `!cool_call && min_run_done` branch idles the compressor once min-run elapses. While min-run is unmet, cooling continues — by design.
- **HvacState::Idle**: re-entry into Cooling requires `cool_call == true`, which it never will be while `windows_open_` is set.
- **HvacState::Heating**: untouched.
- Fan-circulate logic (later in tick) is untouched and continues to run.

## Testing

Native unit tests (`src/tests/test_controller_runtime.cpp`):
1. `cool_call` is suppressed when `windows_open` is set; runtime stays in Idle.
2. `heat_call` is unaffected when `windows_open` is set.
3. Active Cooling honors min-run-time before idling on `windows_open=true`.
4. Cannot re-enter Cooling while `windows_open=true`, even after min-off-time has elapsed.
5. Fan-circulate continues to run while `windows_open=true` in Cool mode.
6. Snapshot reflects `windows_open` correctly across set/clear.

Display unit tests (`thermostat_ui_state`):
1. `"Windows Open"` returned when `mode == Cool && windows_open == true` and no higher-priority state active.
2. Higher-priority states (Failsafe, Locked Out, Not Connected) override.
3. Heat or Off mode with `windows_open == true` returns normal mode/state text.

Integration (manual / sim):
- Toggle windows-open in sim while in Cool mode; observe display status flip and compressor idle after min-run.
- HA discovery surfaces the new switch entity.

## Out of Scope

- Persistence of `windows_open` across controller reboot (HA's retained MQTT topic handles this).
- Per-zone or multiple window-sensor handling (HA aggregates upstream).
- Automatic mode switching (we never modify the user's mode selection).
- Suppressing fan when windows are open.
- Adding `windows_open` to the audit-log-only diagnostics; standard transition audit lines are sufficient.

## Open Items for Implementation Plan

1. Verify free bit in `espnow_cmd_word.h` packed snapshot word. If none, design the protocol-version bump and coordinated rollout (controller + display flashed together).
2. Confirm exact `furnace_state_text` signature change — pass `bool windows_open` + `FurnaceMode mode` as additional params, or refactor the function to take a `ThermostatSnapshot` + connection bool. Latter is cleaner; affects all callers.
3. HA discovery payload shape for `switch` entity (copy `hvac_lockout` discovery as template).
