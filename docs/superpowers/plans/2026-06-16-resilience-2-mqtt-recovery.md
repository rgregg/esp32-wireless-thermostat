# Resilience Increment 2: MQTT Reconnect via NetworkRecoveryPolicy

> Implement with subagent-driven-development. Steps use `- [ ]`.

**Goal:** Replace the controller's fixed 5 s MQTT reconnect retry with `NetworkRecoveryPolicy`-driven **capped exponential backoff + subsystem-restart escalation** — so a dead/unreachable broker is no longer hammered every 5 s, and the MQTT client is re-initialised on repeated failure. Device reboot stays owned by the existing isolation watchdog (the MQTT policy runs with `reboot_enabled=false`).

**Tech Stack:** C++ (Arduino-ESP32 / native), the `NetworkRecoveryPolicy` from increment 1.

**Validation:** Part A (policy `reboot_enabled`) is native-tested. Part B (firmware wiring) is compile-verified here; its runtime behavior is validated on the Feather bench in a later step (needs the full controller-s3 firmware on the Feather + guest WiFi).

---

## Part A — add `reboot_enabled` to the policy (native, TDD)

**Files:** Modify `include/controller/network_recovery_policy.h`, `src/controller/network_recovery_policy.cpp`, `src/tests/test_network_recovery_policy.cpp`.

- [ ] **Step A1:** In `NetworkRecoveryConfig` (header), add `bool reboot_enabled = true;` (after `restarts_before_reboot`).

- [ ] **Step A2:** Add a failing test to `test_network_recovery_policy.cpp`:

```cpp
TEST_CASE(recovery_reboot_disabled_keeps_restarting_never_reboots) {
  NetworkRecoveryConfig c = cfg();   // restarts_before_reboot=2
  c.reboot_enabled = false;
  NetworkRecoveryPolicy p(c);
  uint32_t t = 0; int restarts = 0, reboots = 0, guard = 0;
  while (guard++ < 200) {
    RecoveryAction a = p.poll(t);
    if (a == RecoveryAction::Connect) p.on_connect_failed();
    else if (a == RecoveryAction::RestartSubsystem) restarts++;
    else if (a == RecoveryAction::Reboot) reboots++;
    t += 10000;
  }
  ASSERT_EQ(reboots, 0);          // never reboots when disabled
  ASSERT_TRUE(restarts >= 3);     // keeps restarting the subsystem instead
}
```

- [ ] **Step A3:** Run `pio run -e native-tests` — expect this new test to FAIL (reboot still fires).

- [ ] **Step A4:** In `poll()` (`.cpp`), in the escalation branch where `restart_count_ > config_.restarts_before_reboot`: only set `reboot_requested_ = true` and return `Reboot` **if `config_.reboot_enabled`**. Otherwise return `RecoveryAction::RestartSubsystem` (do not set `reboot_requested_`). Keep everything else (counter resets, backoff reset) the same.

- [ ] **Step A5:** Run `pio run -e native-tests && ./.pio/build/native-tests/program` — expect PASS, count = 167 (was 166, +1). All existing policy tests still pass (the `reboot_is_terminal` test uses the default `reboot_enabled=true`).

- [ ] **Step A6:** Commit:
```bash
git add include/controller/network_recovery_policy.h src/controller/network_recovery_policy.cpp src/tests/test_network_recovery_policy.cpp
git commit -m "feat(controller): NetworkRecoveryConfig.reboot_enabled (cap escalation at restart)"
```

---

## Part B — wire the policy into MQTT reconnect (firmware)

**Files:** Modify `src/esp32_controller_main.cpp`.

- [ ] **Step B1:** Add the include near the other `controller/...` includes:
```cpp
#include "controller/network_recovery_policy.h"
```
and a module-global policy near the other `g_ctrl_*` globals (reboot disabled — the isolation watchdog owns device reboots):
```cpp
thermostat::NetworkRecoveryPolicy g_ctrl_mqtt_recovery(thermostat::NetworkRecoveryConfig{
    /*base_backoff_ms=*/1000, /*max_backoff_ms=*/60000,
    /*fails_before_restart=*/5, /*restarts_before_reboot=*/0,
    /*reboot_enabled=*/false});
```
(Use aggregate init matching the struct's field order. Confirm the field order in the header and match it; if designated initializers aren't accepted by the project's C++ standard, use positional `{1000, 60000, 5, 0, false}`.)

- [ ] **Step B2:** Rewrite the retry-gating + outcome handling in `ctrl_ensure_mqtt_connected(now_ms)`. Read the current function first. Keep the early returns for: host not configured / not enabled; the `g_ctrl_mqtt_reconfigure_required` block; and `WiFi.status() != WL_CONNECTED`. Then:
  - **Detect a drop:** if `g_ctrl_mqtt_recovery.connected() && !g_ctrl_mqtt.connected()`, call `g_ctrl_mqtt_recovery.on_disconnected();`.
  - **Already connected:** if `g_ctrl_mqtt.connected()`, just `return;` (it stays connected; policy already knows).
  - **Replace** the `if ((now_ms - g_ctrl_last_mqtt_attempt_ms) < kCtrlNetworkRetryMs) return;` gate with:
    ```cpp
    const thermostat::RecoveryAction act = g_ctrl_mqtt_recovery.poll(now_ms);
    if (act == thermostat::RecoveryAction::None) return;
    if (act == thermostat::RecoveryAction::RestartSubsystem) {
      // Re-initialise the MQTT client before the next attempt.
      g_ctrl_mqtt.disconnect();
      g_ctrl_mqtt.setServer(g_cfg_ctrl_mqtt_host.c_str(), g_cfg_ctrl_mqtt_port);
      g_ctrl_mqtt.setSocketTimeout(kCtrlMqttSocketTimeoutS);
      ctrl_audit("mqtt: subsystem restart (recovery escalation)");
      return;  // attempt the connect on the next poll
    }
    // act == Connect (Reboot cannot occur — reboot_enabled is false)
    ```
  - Keep `g_ctrl_last_mqtt_attempt_ms = now_ms;` and the existing `setServer` + `connect(...)` attempt.
  - **On the connect result:** after the existing `if (!ok) { ...; return; }`, the failure path must call `g_ctrl_mqtt_recovery.on_connect_failed();` BEFORE its `return`. On success (after `g_ctrl_last_mqtt_error = "none";`), call `g_ctrl_mqtt_recovery.on_connected();`.
  - Leave the subscribe block and everything after unchanged.

- [ ] **Step B3:** Build to confirm it compiles (this is the live controller — be precise):
  - `pio run -e esp32-furnace-controller` → SUCCESS.
  - `pio run -e esp32-furnace-controller-s3` → SUCCESS (the bench firmware; first hybrid build may be slow).
  - `pio run -e native-tests && ./.pio/build/native-tests/program` → 167 pass.

- [ ] **Step B4:** Commit:
```bash
git add src/esp32_controller_main.cpp
git commit -m "feat(controller): drive MQTT reconnect via NetworkRecoveryPolicy (backoff + restart)"
```

---

## Self-Review
- **Spec coverage:** Implements the MQTT slice of spec §3 (bounded backoff + subsystem restart instead of a fixed retry; reboot left to the isolation watchdog).
- **Risk:** Part B changes the LIVE controller's MQTT reconnect path. It's compile-verified here; **runtime behavior MUST be validated on the Feather bench before merge** (MQTT unreachable on guest WiFi → confirm growing backoff in the audit log, periodic "subsystem restart", no tight loop, control loop unaffected, no device reboot). Note this in the report.
- **Type consistency:** `RecoveryAction`/`NetworkRecoveryConfig` field order/names match the header; `on_connected`/`on_connect_failed`/`on_disconnected`/`poll` used as defined.
