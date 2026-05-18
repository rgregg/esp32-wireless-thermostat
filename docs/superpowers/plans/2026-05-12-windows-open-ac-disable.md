# Windows Open A/C Disable — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `windows_open` boolean state, driven by Home Assistant over MQTT, that suppresses cooling demand only. Display shows "Windows Open" in status text when active mode is Cool.

**Architecture:** New `windows_open_` field on `ControllerRuntime` mirroring the `hvac_lockout` plumbing pattern. Cooling suppression is implemented by forcing `cool_call = false` at the top of `apply_hvac_calls`, after the failsafe/lockout early-return — so existing min-run-time logic naturally finishes any active cooling cycle before idling. Wire transport: MQTT topic `cmd/windows_open` for HA, JSON field in retained state, and a single repurposed bit in the existing ESP-NOW `lockout` byte (bit 0 = lockout, bit 1 = windows_open) to avoid a protocol-version bump. Display parses both sources and `furnace_state_text` returns "Windows Open" only when active mode is Cool and no higher-priority state is present.

**Tech Stack:** C++ / Arduino / ESP-IDF (ESP32), PlatformIO, LVGL (display), MQTT (PubSubClient), ESP-NOW.

**Spec:** [`docs/superpowers/specs/2026-05-12-windows-open-ac-disable-design.md`](../specs/2026-05-12-windows-open-ac-disable-design.md)

---

## Task 1: Add `windows_open` to shared snapshot type and `ControllerRuntime`

Adds the field, accessors, and the cooling-suppression branch in `apply_hvac_calls`. Drives the change with native unit tests in `test_controller_runtime.cpp`.

**Files:**
- Modify: `include/thermostat_types.h`
- Modify: `include/controller/controller_runtime.h`
- Modify: `src/controller/controller_runtime.cpp`
- Modify: `src/tests/test_controller_runtime.cpp`

- [ ] **Step 1: Write failing test — cool_call is suppressed when windows_open is set**

Append to `src/tests/test_controller_runtime.cpp`:

```cpp
TEST_CASE(controller_runtime_windows_open_suppresses_cooling) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 1000000;
  cfg.min_idle_time_ms = 0;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  thermostat::ControllerRuntime rt(cfg);
  rt.note_heartbeat(1);

  // Put runtime in Cool mode via remote command
  CommandWord cmd;
  cmd.mode = FurnaceMode::Cool;
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);

  // Without windows_open, cool_call drives cool demand
  thermostat::ControllerTickInput t1;
  t1.now_ms = 1000;
  t1.cool_call = true;
  t1.has_indoor_temp = true;
  rt.tick(t1);
  ASSERT_TRUE(rt.cool_demand());

  // Snapshot reflects windows_open=false initially
  ASSERT_TRUE(!rt.snapshot().windows_open);

  // Activate windows_open — cool demand must drop (no min-run configured)
  rt.set_windows_open(true);
  thermostat::ControllerTickInput t2;
  t2.now_ms = 2000;
  t2.cool_call = true;
  t2.has_indoor_temp = true;
  rt.tick(t2);
  ASSERT_TRUE(rt.windows_open());
  ASSERT_TRUE(rt.snapshot().windows_open);
  ASSERT_TRUE(!rt.cool_demand());

  // Clearing windows_open re-enables cooling
  rt.set_windows_open(false);
  thermostat::ControllerTickInput t3;
  t3.now_ms = 3000;
  t3.cool_call = true;
  t3.has_indoor_temp = true;
  rt.tick(t3);
  ASSERT_TRUE(rt.cool_demand());
}

TEST_CASE(controller_runtime_windows_open_does_not_block_heat) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 1000000;
  cfg.min_idle_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  thermostat::ControllerRuntime rt(cfg);
  rt.note_heartbeat(1);

  CommandWord cmd;
  cmd.mode = FurnaceMode::Heat;
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);

  rt.set_windows_open(true);
  thermostat::ControllerTickInput t1;
  t1.now_ms = 1000;
  t1.heat_call = true;
  t1.has_indoor_temp = true;
  rt.tick(t1);
  ASSERT_TRUE(rt.heat_demand());
}

TEST_CASE(controller_runtime_windows_open_honors_cool_min_run_time) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 1000000;
  cfg.min_idle_time_ms = 0;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 60000;  // 60s
  thermostat::ControllerRuntime rt(cfg);
  rt.note_heartbeat(1);

  CommandWord cmd;
  cmd.mode = FurnaceMode::Cool;
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);

  // Enter cooling
  thermostat::ControllerTickInput t1;
  t1.now_ms = 1000;
  t1.cool_call = true;
  t1.has_indoor_temp = true;
  rt.tick(t1);
  ASSERT_TRUE(rt.cool_demand());

  // Windows open while min_run not yet elapsed — still cooling
  rt.set_windows_open(true);
  thermostat::ControllerTickInput t2;
  t2.now_ms = 30000;
  t2.cool_call = true;  // upstream still requesting
  t2.has_indoor_temp = true;
  rt.tick(t2);
  ASSERT_TRUE(rt.cool_demand());

  // After min_run elapses, cooling idles
  thermostat::ControllerTickInput t3;
  t3.now_ms = 70000;
  t3.cool_call = true;
  t3.has_indoor_temp = true;
  rt.tick(t3);
  ASSERT_TRUE(!rt.cool_demand());
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: 3 new test cases fail with compile error — `windows_open`, `set_windows_open`, and `ThermostatSnapshot::windows_open` not declared.

- [ ] **Step 3: Add `windows_open` to `ThermostatSnapshot`**

In `include/thermostat_types.h`, modify the snapshot struct:

```cpp
struct ThermostatSnapshot {
  FurnaceMode mode = FurnaceMode::Off;
  FanMode fan_mode = FanMode::Automatic;
  RelayDemand relay{};
  bool hvac_lockout = false;
  bool failsafe_active = false;
  bool windows_open = false;
};
```

- [ ] **Step 4: Add `windows_open_` field and accessors to `ControllerRuntime`**

In `include/controller/controller_runtime.h`, after `void set_hvac_lockout(bool locked_out);` (around line 52) add:

```cpp
  void set_windows_open(bool open);
```

After `bool hvac_lockout() const { return hvac_lockout_; }` (around line 71) add:

```cpp
  bool windows_open() const { return windows_open_; }
```

In the private members section, after `bool hvac_lockout_ = false;` (around line 114) add:

```cpp
  bool windows_open_ = false;
```

- [ ] **Step 5: Implement `set_windows_open` and snapshot population**

In `src/controller/controller_runtime.cpp`, after the `set_hvac_lockout` function (around line 61), add:

```cpp
void ControllerRuntime::set_windows_open(bool open) {
  if (open != windows_open_) {
    audit("windows_open: %s [internal]", open ? "on" : "off");
  }
  windows_open_ = open;
}
```

In `ControllerRuntime::snapshot()` (around line 208–215), add the new field:

```cpp
ThermostatSnapshot ControllerRuntime::snapshot() const {
  ThermostatSnapshot s;
  s.mode = mode_;
  s.fan_mode = fan_mode_;
  s.relay = relay_;
  s.hvac_lockout = hvac_lockout_;
  s.failsafe_active = failsafe_active_;
  s.windows_open = windows_open_;
  return s;
}
```

- [ ] **Step 6: Suppress `cool_call` in `apply_hvac_calls`**

In `src/controller/controller_runtime.cpp`, in `apply_hvac_calls` (around line 248), modify the function so that immediately after the `failsafe_active_ || hvac_lockout_` early-return block, the parameter `cool_call` is forced false when windows are open. Because `cool_call` is a value parameter, we mutate the local copy.

Find:

```cpp
void ControllerRuntime::apply_hvac_calls(uint32_t now_ms, bool heat_call, bool cool_call) {
  if (failsafe_active_ || hvac_lockout_) {
    enter_idle(now_ms);
    relay_.fan = false;
    equipment_delay_ = false;
    return;
  }

  switch (hvac_state_) {
```

Replace with:

```cpp
void ControllerRuntime::apply_hvac_calls(uint32_t now_ms, bool heat_call, bool cool_call) {
  if (failsafe_active_ || hvac_lockout_) {
    enter_idle(now_ms);
    relay_.fan = false;
    equipment_delay_ = false;
    return;
  }

  if (windows_open_) {
    cool_call = false;
  }

  switch (hvac_state_) {
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All tests pass, including the three new windows_open cases.

- [ ] **Step 8: Commit**

```bash
git add include/thermostat_types.h include/controller/controller_runtime.h \
        src/controller/controller_runtime.cpp src/tests/test_controller_runtime.cpp
git commit -m "feat(controller): add windows_open state suppressing cooling demand"
```

---

## Task 2: Expose `windows_open` through `ControllerApp` and telemetry struct

Adds the app-level pass-through and surfaces the flag on outgoing telemetry so MQTT and ESP-NOW publishers can carry it. Includes change-detection so telemetry republishes when the flag flips.

**Files:**
- Modify: `include/controller/controller_app.h`
- Modify: `src/controller/controller_app.cpp`

- [ ] **Step 1: Add `windows_open` to `ControllerTelemetry`**

In `include/controller/controller_app.h`, around line 11–19, add a new field at the end of the struct:

```cpp
struct ControllerTelemetry {
  uint16_t seq = 0;
  FurnaceStateCode state = FurnaceStateCode::Error;
  float filter_runtime_hours = 0.0f;
  bool lockout = false;
  uint8_t mode_code = 0;  // off=0, heat=1, cool=2
  uint8_t fan_code = 0;   // auto=0, on=1, circulate=2
  float setpoint_c = 0.0f;
  bool windows_open = false;
};
```

- [ ] **Step 2: Add `set_windows_open` method on `ControllerApp`**

In `include/controller/controller_app.h`, after `void set_hvac_lockout(bool locked);` (around line 41), add:

```cpp
  void set_windows_open(bool open);
```

- [ ] **Step 3: Implement `set_windows_open` pass-through**

In `src/controller/controller_app.cpp`, after the `set_hvac_lockout` function (around line 73–76), add:

```cpp
void ControllerApp::set_windows_open(bool open) {
  runtime_.set_windows_open(open);
  publish();
}
```

- [ ] **Step 4: Populate `windows_open` in `publish()` and update change detection**

In `src/controller/controller_app.cpp`, modify `telemetry_payload_changed` (around line 37–45) to include the new field:

```cpp
bool ControllerApp::telemetry_payload_changed(const ControllerTelemetry &next) const {
  if (!has_last_published_) {
    return true;
  }
  return next.state != last_published_.state ||
         next.filter_runtime_hours != last_published_.filter_runtime_hours ||
         next.lockout != last_published_.lockout || next.mode_code != last_published_.mode_code ||
         next.fan_code != last_published_.fan_code || next.setpoint_c != last_published_.setpoint_c ||
         next.windows_open != last_published_.windows_open;
}
```

In `publish()` (around line 202–224), populate the new field. Find:

```cpp
  t.lockout = runtime_.hvac_lockout();
  t.mode_code = mode_to_code(runtime_.mode());
  t.fan_code = fan_to_code(runtime_.fan_mode());
  t.setpoint_c = runtime_.target_temperature_c();
```

Add after the last line:

```cpp
  t.windows_open = runtime_.windows_open();
```

- [ ] **Step 5: Run native tests to verify nothing regressed**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/controller/controller_app.h src/controller/controller_app.cpp
git commit -m "feat(controller): plumb windows_open through ControllerApp and telemetry"
```

---

## Task 3: Carry `windows_open` over ESP-NOW (bit-packed in existing `lockout` byte)

Avoids a protocol version bump by using bit 1 of the `lockout` byte for `windows_open` (bit 0 remains the lockout flag). Receivers running older firmware see the windows_open bit as garbage in `lockout`, so the encoder MUST encode only the boolean in bit 0 and bit 1 — no other bits. Decoders MUST mask `& 0x01` for lockout. Old senders set bit 1 to 0, so new receivers see windows_open=false from old senders, which is the safe default.

**Files:**
- Modify: `include/thermostat/transport/espnow_thermostat_transport.h`
- Modify: `src/controller/transport/espnow_controller_transport.cpp`
- Modify: `src/thermostat/transport/espnow_thermostat_transport.cpp`

- [ ] **Step 1: Add `windows_open` to `ThermostatControllerTelemetry`**

In `include/thermostat/transport/espnow_thermostat_transport.h`, modify the struct around lines 13–21:

```cpp
struct ThermostatControllerTelemetry {
  uint16_t seq = 0;
  FurnaceStateCode state = FurnaceStateCode::Error;
  bool lockout = false;
  uint8_t mode_code = 0;
  uint8_t fan_code = 0;
  float setpoint_c = 0.0f;
  uint32_t filter_runtime_seconds = 0;
  bool windows_open = false;
};
```

- [ ] **Step 2: Encode windows_open in lockout byte on the controller transmitter**

In `src/controller/transport/espnow_controller_transport.cpp` `publish_telemetry` (around line 153), find:

```cpp
  pkt.lockout = telemetry.lockout ? 1 : 0;
```

Replace with:

```cpp
  pkt.lockout = static_cast<uint8_t>((telemetry.lockout ? 0x01 : 0x00) |
                                     (telemetry.windows_open ? 0x02 : 0x00));
```

- [ ] **Step 3: Decode bit fields on the display receiver**

In `src/thermostat/transport/espnow_thermostat_transport.cpp` `on_recv` (around line 220), find:

```cpp
        telemetry.lockout = pkt->lockout != 0;
```

Replace with:

```cpp
        telemetry.lockout = (pkt->lockout & 0x01) != 0;
        telemetry.windows_open = (pkt->lockout & 0x02) != 0;
```

- [ ] **Step 4: Wire windows_open through the controller transport caller**

We also need the controller's `ControllerNode` (the layer that builds `ControllerTelemetry`-equivalent data for the transport) to populate the new field. Find the call sites that build the transport's telemetry struct:

Run: `grep -rn "publish_telemetry\|ControllerTelemetry t\|ControllerTelemetry tel" src/controller include/controller`

Find the location that constructs a `ControllerTelemetry` for transport (already happens in `ControllerApp::publish()` from Task 2). Confirm `ControllerApp::publish()` passes the struct to `transport_.publish_telemetry(t)` — it does. Since `IControllerTransport::publish_telemetry` accepts the same `ControllerTelemetry` struct that now has `windows_open`, no further wiring is needed on the controller send side.

On the display, the transport currently calls back with `ThermostatControllerTelemetry` which now also carries `windows_open`. Confirm by reading.

Run: `grep -rn "on_telemetry\b\|ThermostatControllerTelemetry" src/thermostat include/thermostat | head`
Expected: `ThermostatNode::on_telemetry` and `ThermostatApp::on_controller_telemetry` are the consumers. We'll update `ThermostatApp` in Task 5.

- [ ] **Step 5: Build native tests and both firmware envs to verify compile**

Run: `pio run -e native-tests`
Run: `pio run -e esp32-furnace-controller`
Run: `pio run -e esp32-furnace-thermostat`
Expected: All three builds succeed.

- [ ] **Step 6: Commit**

```bash
git add include/thermostat/transport/espnow_thermostat_transport.h \
        src/controller/transport/espnow_controller_transport.cpp \
        src/thermostat/transport/espnow_thermostat_transport.cpp
git commit -m "feat(transport): carry windows_open in ESP-NOW lockout byte (bit 1)"
```

---

## Task 4: Controller MQTT — command topic, retained state, discovery, status page

Mirrors the existing `cmd/lockout` plumbing: subscribe to `cmd/windows_open`, publish retained `state/windows_open`, publish HA discovery for a `switch` entity, add to the JSON status payload, and add a row on the status page.

**Files:**
- Modify: `src/esp32_controller_main.cpp`

- [ ] **Step 1: Add MQTT command handler for `cmd/windows_open`**

In `src/esp32_controller_main.cpp`, around line 1873–1892, find the lockout command block:

```cpp
  // Block lockout and HVAC commands when HA is not allowed
  if (!ha_allowed) {
    if (topic_str == self_topic_for("cmd/lockout") ||
        topic_str == self_topic_for("cmd/mode") ||
```

Modify the gate list to also block `cmd/windows_open`:

```cpp
  if (!ha_allowed) {
    if (topic_str == self_topic_for("cmd/lockout") ||
        topic_str == self_topic_for("cmd/windows_open") ||
        topic_str == self_topic_for("cmd/mode") ||
```

Then, immediately after the existing `if (topic_str == self_topic_for("cmd/lockout"))` block (which ends with `return;` around line 1892), add:

```cpp
  if (topic_str == self_topic_for("cmd/windows_open")) {
    const bool new_value = mqtt_payload::parse_bool(value);
    ctrl_audit("windows_open: %s [mqtt]", new_value ? "on" : "off");
    g_controller->app().set_windows_open(new_value);
    ctrl_publish_runtime_state();
    return;
  }
```

- [ ] **Step 2: Subscribe to the new command topic on MQTT connect**

In `src/esp32_controller_main.cpp` around line 2035–2046, find the `subs_ok` chain. Modify to add the new subscription right after the `cmd/lockout` line:

```cpp
  const bool subs_ok =
      g_ctrl_mqtt.subscribe(self_topic_for("cmd/lockout").c_str()) &&
      g_ctrl_mqtt.subscribe(self_topic_for("cmd/windows_open").c_str()) &&
      g_ctrl_mqtt.subscribe(self_topic_for("cmd/mode").c_str()) &&
```

- [ ] **Step 3: Publish retained state**

In `src/esp32_controller_main.cpp` `ctrl_publish_runtime_state()` around line 1586, find:

```cpp
  g_ctrl_mqtt.publish(self_topic_for("state/lockout").c_str(), lockout ? "1" : "0", true);
```

Add the new publish immediately after:

```cpp
  g_ctrl_mqtt.publish(self_topic_for("state/windows_open").c_str(),
                      rt.windows_open() ? "1" : "0", true);
```

- [ ] **Step 4: Add HA discovery for the windows_open switch**

In `ctrl_publish_discovery()` around line 808, add a new topic constant near the lockout one:

```cpp
  const String switch_topic = dp + "switch/" + dev_id + "_lockout/config";
  const String windows_open_topic = dp + "switch/" + dev_id + "_windows_open/config";
```

Then, after the lockout switch payload publish (around line 865–870), add:

```cpp
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Windows Open\",\"uniq_id\":\"%s_windows_open\","
           "\"cmd_t\":\"%s/cmd/windows_open\","
           "\"stat_t\":\"%s/state/windows_open\",\"pl_on\":\"1\",\"pl_off\":\"0\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(windows_open_topic.c_str(), payload, true);
```

- [ ] **Step 5: Add JSON field to status payload**

In the runtime status JSON formatter around line 1207–1208, find:

```cpp
    "\"hvac_lockout\":%s,"
    "\"failsafe_active\":%s,"
```

Insert after `hvac_lockout`:

```cpp
    "\"hvac_lockout\":%s,"
    "\"windows_open\":%s,"
    "\"failsafe_active\":%s,"
```

Then in the matching `snprintf` arg list around line 1237–1238, find:

```cpp
    rt.hvac_lockout() ? "true" : "false",
    rt.failsafe_active() ? "true" : "false",
```

Insert the new argument between them:

```cpp
    rt.hvac_lockout() ? "true" : "false",
    rt.windows_open() ? "true" : "false",
    rt.failsafe_active() ? "true" : "false",
```

- [ ] **Step 6: Add status-page row**

In `src/esp32_controller_main.cpp` around line 1314, find:

```cpp
  status_item(html, "HVAC Lockout", "hvac_lockout");
```

Add immediately after:

```cpp
  status_item(html, "Windows Open", "windows_open");
```

- [ ] **Step 7: Build controller firmware**

Run: `pio run -e esp32-furnace-controller`
Expected: build succeeds.

- [ ] **Step 8: Commit**

```bash
git add src/esp32_controller_main.cpp
git commit -m "feat(controller): MQTT command, state, discovery, and status row for windows_open"
```

---

## Task 5: Display — parse `windows_open` from MQTT and ESP-NOW, store in app state

Adds the field on `ThermostatApp` and the two paths that populate it: ESP-NOW telemetry (`on_controller_telemetry`) and the MQTT state-update bridge (`on_controller_state_update`).

**Files:**
- Modify: `include/thermostat/thermostat_app.h`
- Modify: `src/thermostat/thermostat_app.cpp`
- Modify: `src/esp32_thermostat_main.cpp`

- [ ] **Step 1: Add `controller_windows_open_` field and accessor to `ThermostatApp`**

In `include/thermostat/thermostat_app.h`, around line 56, after:

```cpp
  bool controller_lockout() const { return controller_lockout_; }
```

Add:

```cpp
  bool controller_windows_open() const { return controller_windows_open_; }
```

Around line 93, after:

```cpp
  bool controller_lockout_ = false;
```

Add:

```cpp
  bool controller_windows_open_ = false;
```

- [ ] **Step 2: Update `on_controller_telemetry` signature is unchanged but body populates the new field**

In `src/thermostat/thermostat_app.cpp` `on_controller_telemetry` around line 54, find:

```cpp
  controller_lockout_ = telemetry.lockout;
```

Add immediately after:

```cpp
  controller_windows_open_ = telemetry.windows_open;
```

- [ ] **Step 3: Extend `on_controller_state_update` to accept `windows_open`**

In `include/thermostat/thermostat_app.h`, find the declaration of `on_controller_state_update` and add a `bool windows_open` parameter at the end. Locate it via:

Run: `grep -n "on_controller_state_update" include/thermostat/thermostat_app.h`

Modify the declaration to:

```cpp
  void on_controller_state_update(uint32_t now_ms, FurnaceStateCode state, bool lockout,
                                  FurnaceMode mode, FanMode fan, float setpoint_c,
                                  uint32_t filter_runtime_seconds, bool windows_open);
```

In `src/thermostat/thermostat_app.cpp` around line 74–84, update the definition and body:

```cpp
void ThermostatApp::on_controller_state_update(
    uint32_t now_ms, FurnaceStateCode state, bool lockout, FurnaceMode mode,
    FanMode fan, float setpoint_c, uint32_t filter_runtime_seconds, bool windows_open) {
  on_controller_heartbeat(now_ms);

  has_controller_telemetry_ = true;
  controller_state_ = state;
  controller_lockout_ = lockout;
  controller_windows_open_ = windows_open;
  controller_setpoint_c_ = setpoint_c;
  controller_filter_runtime_seconds_ = filter_runtime_seconds;
```

- [ ] **Step 4: Update all call sites of `on_controller_state_update`**

Run: `grep -rn "on_controller_state_update" src include`
For each call site (likely in `src/esp32_thermostat_main.cpp` and test files), add a final argument carrying the parsed `windows_open` value from the controller's MQTT state JSON / `state/windows_open` retained topic.

In `src/esp32_thermostat_main.cpp`, locate the parser that reads `state/lockout`. Mirror it with a parser for `state/windows_open`. For example:

Run: `grep -n "state/lockout\b" src/esp32_thermostat_main.cpp`

For each match: add a parallel branch for `state/windows_open` that stores the parsed bool in a local variable used in the subsequent `app_.on_controller_state_update(...)` call. If `on_controller_state_update` is called from multiple places, ensure a `bool g_disp_controller_windows_open = false;` global (or struct field) holds the most recent value and is passed to every call site.

- [ ] **Step 5: Update test call sites of `on_controller_state_update` and `ThermostatControllerTelemetry`**

Run: `grep -rn "on_controller_state_update\|ThermostatControllerTelemetry " src/tests`

For each match, add the new field. For struct literals add `telem.windows_open = false;` where appropriate. For function calls add the final argument `false` unless the test specifically wants to exercise the windows_open path.

- [ ] **Step 6: Build native tests and both firmwares**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Run: `pio run -e esp32-furnace-controller`
Run: `pio run -e esp32-furnace-thermostat`
Expected: all builds succeed, all tests pass.

- [ ] **Step 7: Commit**

```bash
git add include/thermostat/thermostat_app.h src/thermostat/thermostat_app.cpp \
        src/esp32_thermostat_main.cpp src/tests/
git commit -m "feat(display): track controller windows_open via ESP-NOW and MQTT"
```

---

## Task 6: Display status text — "Windows Open" when in Cool mode

Updates `furnace_state_text` to accept the windows_open flag and the active mode. Returns "Windows Open" only when active mode is Cool, windows_open is true, and no higher-priority state (Failsafe, Locked Out, Not Connected) is present.

**Files:**
- Modify: `include/thermostat/thermostat_ui_state.h`
- Modify: `src/thermostat/thermostat_ui_state.cpp`
- Modify: `src/thermostat/thermostat_display_app.cpp`
- Modify: `src/tests/` (add a new test file for thermostat_ui_state or extend existing)

- [ ] **Step 1: Identify or create the test file for thermostat_ui_state**

Run: `ls src/tests/ | grep ui_state`
If no test file exists, create `src/tests/test_thermostat_ui_state.cpp`. Otherwise extend the existing file.

- [ ] **Step 2: Write failing tests for status text**

Add to `src/tests/test_thermostat_ui_state.cpp` (create if missing):

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat/thermostat_ui_state.h"
#include "test_harness.h"

TEST_CASE(furnace_state_text_windows_open_in_cool_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Windows Open");
}

TEST_CASE(furnace_state_text_windows_open_ignored_in_heat_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::HeatMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Heat);
  ASSERT_STR_EQ(s.c_str(), "Heat mode");
}

TEST_CASE(furnace_state_text_windows_open_ignored_in_off_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::Idle, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Off);
  ASSERT_STR_EQ(s.c_str(), "Idle");
}

TEST_CASE(furnace_state_text_failsafe_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/true, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Failsafe");
}

TEST_CASE(furnace_state_text_lockout_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/true,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Locked Out");
}

TEST_CASE(furnace_state_text_disconnected_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/false, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Not Connected");
}

TEST_CASE(furnace_state_text_no_windows_open_returns_normal) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolOn, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/false, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Cool on");
}
#endif
```

If creating the file, also add it to `platformio.ini` test sources if the project uses an explicit list — check by running:

Run: `grep -n "test_thermostat" platformio.ini`
Most likely tests are auto-discovered from `src/tests/`. If so, no `platformio.ini` change is needed.

- [ ] **Step 3: Run tests to verify they fail**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: Compile error — `furnace_state_text` doesn't accept the additional arguments.

- [ ] **Step 4: Update `furnace_state_text` signature**

In `include/thermostat/thermostat_ui_state.h` (around line 9–13), update the declaration:

```cpp
std::string furnace_state_text(FurnaceStateCode state,
                               bool connected,
                               bool lockout,
                               bool failsafe_active,
                               bool windows_open,
                               FurnaceMode mode);
```

In `src/thermostat/thermostat_ui_state.cpp`, update the definition (around lines 5–39) to:

```cpp
std::string furnace_state_text(FurnaceStateCode state,
                               bool connected,
                               bool lockout,
                               bool failsafe_active,
                               bool windows_open,
                               FurnaceMode mode) {
  if (failsafe_active) {
    return "Failsafe";
  }
  if (lockout) {
    return "Locked Out";
  }
  if (!connected) {
    return "Not Connected";
  }
  if (windows_open && mode == FurnaceMode::Cool) {
    return "Windows Open";
  }

  switch (state) {
    case FurnaceStateCode::Idle:
      return "Idle";
    case FurnaceStateCode::HeatMode:
      return "Heat mode";
    case FurnaceStateCode::HeatOn:
      return "Heat on";
    case FurnaceStateCode::CoolMode:
      return "Cool mode";
    case FurnaceStateCode::CoolOn:
      return "Cool on";
    case FurnaceStateCode::FanOn:
      return "Fan on";
    case FurnaceStateCode::HeatWait:
    case FurnaceStateCode::CoolWait:
      return "Waiting for equipment";
    case FurnaceStateCode::Error:
    default:
      return "Error";
  }
}
```

- [ ] **Step 5: Update the `status_text` caller in `thermostat_display_app.cpp`**

In `src/thermostat/thermostat_display_app.cpp` `status_text` (around lines 60–67), replace the body:

```cpp
std::string ThermostatDisplayApp::status_text(uint32_t now_ms,
                                              uint32_t connection_timeout_ms) const {
  const bool connected = app_.controller_connected(now_ms, connection_timeout_ms);
  const bool has_tel = app_.has_controller_telemetry();
  const bool lockout = has_tel ? app_.controller_lockout() : false;
  const bool windows_open = has_tel ? app_.controller_windows_open() : false;
  const auto state =
      has_tel ? app_.controller_state() : FurnaceStateCode::Error;
  const FurnaceMode mode = app_.local_mode();
  return furnace_state_text(state, connected, lockout, /*failsafe_active=*/false,
                            windows_open, mode);
}
```

Confirm that `app_.local_mode()` exists — it's the user's currently-selected mode on the display. If the method is named differently (e.g. `mode()` or `controller_mode()`), use that instead.

Run: `grep -n "local_mode\|FurnaceMode " include/thermostat/thermostat_app.h`

If `local_mode()` doesn't exist, use `app_.controller_mode()` if available, or whichever accessor returns the active `FurnaceMode`.

- [ ] **Step 6: Run tests to verify they pass**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All new and existing tests pass.

- [ ] **Step 7: Build display firmware**

Run: `pio run -e esp32-furnace-thermostat`
Expected: build succeeds.

- [ ] **Step 8: Commit**

```bash
git add include/thermostat/thermostat_ui_state.h src/thermostat/thermostat_ui_state.cpp \
        src/thermostat/thermostat_display_app.cpp src/tests/test_thermostat_ui_state.cpp
git commit -m "feat(display): show 'Windows Open' in status text when active mode is Cool"
```

---

## Task 7: Simulator — toggle key and MQTT command for `windows_open`

Adds parity with the lockout toggle so the sim can drive the new state.

**Files:**
- Modify: `src/sim/controller_preview.cpp`

- [ ] **Step 1: Add MQTT command handler for `cmd/windows_open`**

In `src/sim/controller_preview.cpp` around line 310, after the `cmd/lockout` handler:

```cpp
  if (topic == self_topic("cmd/lockout")) {
    g_app->set_hvac_lockout(mqtt_payload::parse_bool(payload.c_str()));
    publish_controller_extras();
    return;
  }
```

Add:

```cpp
  if (topic == self_topic("cmd/windows_open")) {
    g_app->set_windows_open(mqtt_payload::parse_bool(payload.c_str()));
    publish_controller_extras();
    return;
  }
```

- [ ] **Step 2: Add a keyboard shortcut to toggle `windows_open`**

In `src/sim/controller_preview.cpp` around lines 827–830, after the lockout toggle:

```cpp
    case SDLK_l:
      g_app->set_hvac_lockout(!g_app->runtime().hvac_lockout());
      publish_controller_extras();
      break;
```

Add:

```cpp
    case SDLK_o:
      g_app->set_windows_open(!g_app->runtime().windows_open());
      publish_controller_extras();
      break;
```

- [ ] **Step 3: Add a status overlay line for windows_open**

In `src/sim/controller_preview.cpp` around line 568, after the existing lockout overlay:

```cpp
  snprintf(buf, sizeof(buf), "Lockout: %s", g_app->runtime().hvac_lockout() ? "ACTIVE" : "OK");
```

Find the corresponding draw call and add a sibling overlay drawn below it:

```cpp
  snprintf(buf, sizeof(buf), "Windows: %s", g_app->runtime().windows_open() ? "OPEN" : "OK");
  // ... mirror the existing draw_text call for the lockout line, offset y by one row;
  //     use kColorRed if windows_open, kColorGreen otherwise.
```

Match the existing positioning and rendering API used for the lockout overlay; do not invent new helpers.

- [ ] **Step 4: Build the sim**

Run: `pio run -e sim-controller-preview` (or whichever sim env exists — check `platformio.ini`).

Run: `grep -n "^\[env:" platformio.ini`
Use the matching sim env name.

Expected: sim build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/sim/controller_preview.cpp
git commit -m "feat(sim): toggle and MQTT command parity for windows_open"
```

---

## Task 8: Full verification and final review

- [ ] **Step 1: Run all native tests**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All tests pass; new windows_open tests are listed in output.

- [ ] **Step 2: Build both firmwares**

Run: `pio run -e esp32-furnace-controller`
Run: `pio run -e esp32-furnace-thermostat`
Expected: both succeed.

- [ ] **Step 3: Manual smoke check (optional, requires hardware)**

If hardware is available:
1. Set HVAC mode to Off and wait 60s per the flashing runbook.
2. Flash both firmwares.
3. From an MQTT CLI: `mosquitto_pub -t "<base>/devices/<ctrl_mac>/cmd/windows_open" -m "1"`.
4. Confirm `state/windows_open` is published as "1".
5. Set mode to Cool, raise indoor temperature above setpoint → confirm cooling does NOT start.
6. Confirm display status text reads "Windows Open".
7. Publish `cmd/windows_open` "0" → cooling resumes after min-off elapses.
8. With windows_open active, switch to Heat → display shows "Heat mode" (no "Windows Open" text).

- [ ] **Step 4: Final commit if any cleanup needed, then push (or open PR per user direction)**

This plan does not push or open a PR; the user will decide whether to merge directly or open a PR.

---

## Self-Review Summary

- **Spec coverage:** All spec sections mapped — runtime suppression (Task 1), app/telemetry plumbing (Task 2), ESP-NOW (Task 3), MQTT command/state/discovery/status page (Task 4), display app field (Task 5), display status text gated to Cool mode (Task 6), sim parity (Task 7), verification (Task 8). The open item "free bit in ESP-NOW packet" is resolved in Task 3 by reusing the `lockout` byte's spare bits (no protocol bump). The open item "furnace_state_text signature" is resolved in Task 6 by adding `bool windows_open` and `FurnaceMode mode` parameters.
- **Placeholders:** None.
- **Type consistency:** `set_windows_open` / `windows_open()` used uniformly across runtime, app, and sim. `ThermostatSnapshot::windows_open`, `ControllerTelemetry::windows_open`, and `ThermostatControllerTelemetry::windows_open` are all `bool`. MQTT topic `cmd/windows_open` and state topic `state/windows_open` used uniformly.
