# MQTT Topic Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Consolidate all MQTT topics under `esp32-wireless-thermostat/devices/{MAC}/...` with MAC-based device paths, removing hardcoded base topic config keys.

**Architecture:** Replace per-device base topic strings with a single shared `base_topic` config + each device's WiFi MAC. Topic helpers become `{base_topic}/devices/{MAC}/{suffix}`. Peer topics are constructed from `{base_topic}/devices/{peer_MAC}/{suffix}`. HA discovery payloads updated to reference new paths.

**Tech Stack:** Arduino/ESP-IDF C++ (PubSubClient), SDL2 simulators (mosquittopp), Python test scripts.

**Design doc:** `docs/plans/2026-03-09-mqtt-topic-redesign.md`

---

### Task 1: Extract shared topic builder into header

Create a platform-agnostic header that both firmware and simulators use to construct topics from base_topic + MAC + suffix. This is the foundation everything else builds on.

**Files:**
- Create: `include/mqtt_topics.h`
- Test: `src/tests/test_mqtt_topics.cpp`

**Step 1: Write the failing test**

Create `src/tests/test_mqtt_topics.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "mqtt_topics.h"

TEST_CASE(mqtt_topics_device_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
      "esp32-wireless-thermostat", "AABBCC", "state/mode");
  ASSERT_STREQ(buf, "esp32-wireless-thermostat/devices/AABBCC/state/mode");
}

TEST_CASE(mqtt_topics_device_topic_truncates_safely) {
  char buf[32];  // too small
  mqtt_topics::device_topic(buf, sizeof(buf),
      "esp32-wireless-thermostat", "AABBCC", "state/mode");
  // Must be null-terminated and not overflow
  ASSERT_TRUE(strlen(buf) < 32);
}

TEST_CASE(mqtt_topics_announce_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
      "esp32-wireless-thermostat", "AABBCC", "announce");
  ASSERT_STREQ(buf, "esp32-wireless-thermostat/devices/AABBCC/announce");
}

TEST_CASE(mqtt_topics_peer_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
      "esp32-wireless-thermostat", "112233", "state/packed_command");
  ASSERT_STREQ(buf, "esp32-wireless-thermostat/devices/112233/state/packed_command");
}

TEST_CASE(mqtt_topics_wildcard_subscribe) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
      "esp32-wireless-thermostat", "+", "announce");
  ASSERT_STREQ(buf, "esp32-wireless-thermostat/devices/+/announce");
}

TEST_CASE(mqtt_topics_client_id) {
  char buf[64];
  mqtt_topics::client_id(buf, sizeof(buf),
      "esp32-wireless-thermostat", "AABBCC");
  ASSERT_STREQ(buf, "esp32-wireless-thermostat-AABBCC");
}

#endif
```

**Step 2: Run tests to verify they fail**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: Compile error — `mqtt_topics.h` not found.

**Step 3: Write the header**

Create `include/mqtt_topics.h`:

```cpp
#pragma once
#include <cstdio>
#include <cstring>

namespace mqtt_topics {

/// Build "{base}/devices/{mac}/{suffix}" into buf.
inline int device_topic(char *buf, size_t buf_len,
                        const char *base, const char *mac,
                        const char *suffix) {
  return snprintf(buf, buf_len, "%s/devices/%s/%s", base, mac, suffix);
}

/// Build "{base}-{mac}" client ID into buf.
inline int client_id(char *buf, size_t buf_len,
                     const char *base, const char *mac) {
  return snprintf(buf, buf_len, "%s-%s", base, mac);
}

}  // namespace mqtt_topics
```

**Step 4: Run tests to verify they pass**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All new tests PASS.

**Step 5: Commit**

```
git add include/mqtt_topics.h src/tests/test_mqtt_topics.cpp
git commit -m "feat: add shared mqtt_topics.h topic builder with tests"
```

---

### Task 2: Add `base_topic`, `ha_discovery_enabled`, and `device_name` config to controller

Replace `mqtt_base_topic`, `display_mqtt_base_topic`, and `mqtt_client_id` config keys with `base_topic`, `ha_discovery_enabled`, and `device_name`. Update NVS load/save and config publishing.

**Files:**
- Modify: `src/esp32_controller_main.cpp`
  - Compile-time defaults (~lines 134-156)
  - Config variables (~lines 191-197)
  - NVS load (~lines 300-310)
  - Topic helper functions (~lines 357-369)
  - Config key handlers (~lines 456-468)
  - Config publishing (~lines 377-418)
  - Web config UI fields (~lines 1320-1330)

**Step 1: Update compile-time defaults**

Replace these defines:
```cpp
// OLD:
#define THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID "esp32-furnace-controller"
#define THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC "thermostat/furnace-controller"
#define THERMOSTAT_THERMOSTAT_MQTT_BASE_TOPIC "thermostat/furnace-display"
#define THERMOSTAT_MQTT_UNIQUE_DEVICE_ID "wireless_thermostat_system"
#define THERMOSTAT_DEVICE_DISCOVERY_PREFIX THERMOSTAT_MQTT_UNIQUE_DEVICE_ID "/devices"

// NEW:
#define THERMOSTAT_BASE_TOPIC "esp32-wireless-thermostat"
```

**Step 2: Update config variables**

Replace:
```cpp
// OLD:
String g_cfg_ctrl_mqtt_client_id;
String g_cfg_ctrl_mqtt_base_topic;
String g_cfg_display_mqtt_base_topic;

// NEW:
String g_cfg_base_topic;
String g_cfg_device_mac;  // 6-char hex, set from WiFi MAC at boot
String g_cfg_device_name; // user-friendly name, default "controller-{MAC}"
bool g_cfg_ha_discovery_enabled = true;
```

**Step 3: Update topic helper functions**

Replace `ctrl_topic_for` and `display_topic_for` with helpers that use `mqtt_topics.h`:

```cpp
#include "mqtt_topics.h"

String device_topic_for(const char *mac, const char *suffix) {
  char buf[192];
  mqtt_topics::device_topic(buf, sizeof(buf),
      g_cfg_base_topic.c_str(), mac, suffix);
  return String(buf);
}

String self_topic_for(const char *suffix) {
  return device_topic_for(g_cfg_device_mac.c_str(), suffix);
}

String peer_topic_for(const char *peer_mac, const char *suffix) {
  return device_topic_for(peer_mac, suffix);
}
```

**Step 4: Update NVS load/save**

- Load `base_topic` from NVS key `"base_topic"` (default: `THERMOSTAT_BASE_TOPIC`)
- Load `ha_discovery_enabled` from NVS key `"ha_disc"` (default: `true`)
- Load `device_name` from NVS key `"device_name"` (default: `"controller-{MAC}"`)
- Remove NVS keys: `"mqtt_cid"`, `"mqtt_base"`, `"disp_base"`
- Compute `g_cfg_device_mac` from `WiFi.macAddress()` (last 3 bytes, uppercase hex, no colons)
- Auto-generate client ID: `mqtt_topics::client_id(buf, ..., base_topic, device_mac)`
- If `device_name` is empty on first boot, auto-generate as `"controller-" + g_cfg_device_mac`

**Step 5: Update config key handlers**

- Add handler for `"base_topic"` → save to NVS, set `g_ctrl_mqtt_discovery_sent = false`
- Add handler for `"ha_discovery_enabled"` → save to NVS
- Add handler for `"device_name"` → save to NVS, set `g_ctrl_mqtt_discovery_sent = false`
- Remove handlers for `"mqtt_client_id"`, `"mqtt_base_topic"`, `"display_mqtt_base_topic"`

**Step 6: Update config publishing and web UI**

- Publish `base_topic` and `ha_discovery_enabled` in `ctrl_publish_all_cfg_state()`
- Update web config form: replace Client ID / Base Topic / Display Base Topic fields with single Base Topic field + HA Discovery Enabled checkbox

**Step 7: Build and verify compilation**

Run: `pio run -e esp32-furnace-controller` (or native-tests for non-hardware validation)
Expected: Compiles without errors. (Full integration comes in later tasks.)

**Step 8: Commit**

```
git commit -m "refactor: replace controller per-device base topics with shared base_topic + MAC"
```

---

### Task 3: Update controller MQTT connect, subscribe, and publish

Wire up all controller MQTT operations to use the new topic structure: `{base_topic}/devices/{MAC}/...`.

**Files:**
- Modify: `src/esp32_controller_main.cpp`
  - MQTT connect/LWT (~line 1949)
  - Subscribe block (~lines 1965-1981)
  - All publish calls in `ctrl_publish_runtime_state()` (~lines 1524-1617)
  - Message callback topic matching (~lines 1710-1845)
  - Device registry publish (~lines 1996-2006)
  - Announce topic (new)

**Step 1: Update MQTT connect**

- LWT topic: `self_topic_for("state/availability")` (same pattern, new path)
- Client ID: auto-generated from `g_cfg_base_topic + "-" + g_cfg_device_mac`

**Step 2: Update subscriptions**

Replace all `ctrl_topic_for("cmd/...")` with `self_topic_for("cmd/...")`.
Replace `display_topic_for("state/packed_command/+")` with wildcard:
`device_topic_for("+", "state/packed_command")`.
Replace `display_topic_for("state/availability")` with:
`device_topic_for("+", "state/availability")`.
Replace `THERMOSTAT_DEVICE_DISCOVERY_PREFIX "/+"` with:
`device_topic_for("+", "announce")`.
Replace sensor subscriptions: `self_topic_for("sensor/+/temp_c")` becomes
`device_topic_for("+", "sensor/temp_c")` and `device_topic_for("+", "sensor/humidity")`.

**Step 3: Update all publish calls**

Replace all `ctrl_topic_for(...)` calls in `ctrl_publish_runtime_state()` with `self_topic_for(...)`.

**Step 4: Update message callback**

The MQTT message callback needs to parse the new topic format. Extract the device MAC from the topic path (`{base}/devices/{MAC}/...`) and use it for:
- Sensor MAC identification (currently parsed from `sensor/{MAC}/temp_c`)
- Packed command source MAC (currently parsed from `packed_command/{MAC}`)
- Authorization checks against the allowed devices list

**Step 5: Publish announce on connect**

After subscribing, publish a retained JSON announce message:
```json
{"role":"controller","firmware":"v0.9.0","name":"controller-29A9C4"}
```
to `self_topic_for("announce")`. The `name` field uses `g_cfg_device_name`.

**Step 6: Guard HA discovery**

Wrap `ctrl_publish_discovery()` call with `if (g_cfg_ha_discovery_enabled)`.

**Step 7: Build and verify**

Run: `pio run -e esp32-furnace-controller`
Expected: Compiles. (Runtime testing after display is also updated.)

**Step 8: Commit**

```
git commit -m "refactor: controller MQTT connect/subscribe/publish uses new topic paths"
```

---

### Task 4: Update controller HA discovery payloads

All discovery payloads must reference the new `{base_topic}/devices/{MAC}/...` paths.

**Files:**
- Modify: `src/esp32_controller_main.cpp` — `ctrl_publish_discovery()` (~lines 837-1017)
- Modify: `src/tests/test_mqtt_discovery.cpp`

**Step 1: Update discovery tests**

Update `kBase` in test file to use the new topic format:
```cpp
static const char *kBase = "esp32-wireless-thermostat/devices/AABBCC";
```

Run tests to verify they fail (payloads still use old format in test expectations).

**Step 2: Update `ctrl_publish_discovery()`**

The `~` (tilde) abbreviation in HA discovery uses the device's full topic path as the base.
Change from `g_cfg_ctrl_mqtt_base_topic` to `self_topic_for("")` (without trailing slash — verify HA tilde expansion behavior).

All topic references in discovery JSON that pointed to `g_cfg_ctrl_mqtt_base_topic` or `g_cfg_display_mqtt_base_topic` must use `self_topic_for(...)` or `peer_topic_for(peer_mac, ...)`.

For display entities (indoor temp/humidity sensors, display settings), the `stat_t` must reference the display's topic path: `{base_topic}/devices/{display_MAC}/...`.

**Step 3: Run discovery tests**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All discovery payload size tests pass with new topic format.

**Step 4: Commit**

```
git commit -m "refactor: HA discovery payloads use new device topic paths"
```

---

### Task 5: Add `base_topic` config to display and update topic helpers

Mirror the controller changes on the display side.

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp`
  - Compile-time defaults (~lines 103-125)
  - Config variables (~lines 295-308)
  - NVS load (~lines 410-441)
  - Topic helpers (~lines 443-455)
  - Config key handlers (~lines 539-546)
  - Config publishing (~lines 470-496)
  - Web config UI fields (~lines 1170-1175)

**Step 1: Update compile-time defaults**

Replace:
```cpp
#define THERMOSTAT_MQTT_CLIENT_ID "esp32-furnace-thermostat"
#define THERMOSTAT_MQTT_BASE_TOPIC "thermostat/furnace-display"
#define THERMOSTAT_MQTT_UNIQUE_DEVICE_ID "wireless_thermostat_system"
#define THERMOSTAT_DEVICE_DISCOVERY_PREFIX ...
```
With:
```cpp
#define THERMOSTAT_BASE_TOPIC "esp32-wireless-thermostat"
```

**Step 2: Update config variables and NVS**

- Replace `g_cfg_mqtt_client_id`, `g_cfg_mqtt_base_topic`, `g_cfg_controller_base_topic` with `g_cfg_base_topic`, `g_cfg_device_mac`, `g_cfg_device_name`, `g_cfg_ha_discovery_enabled`
- Load `device_name` from NVS key `"device_name"` (default: `"display-{MAC}"`)
- Store paired controller MAC in config (existing `devices` mechanism or new `controller_mac` key)
- Auto-generate client ID from base_topic + MAC

**Step 3: Update topic helpers**

Replace `topic_for()` and `controller_topic_for()` with the same pattern as controller:
```cpp
#include "mqtt_topics.h"

String self_topic_for(const char *suffix) { ... }
String controller_topic_for_mac(const char *suffix) {
  return device_topic_for(g_cfg_paired_controller_mac.c_str(), suffix);
}
```

**Step 4: Update config publishing and web UI**

Same pattern as controller Task 2.

**Step 5: Build and verify**

Run: `pio run -e esp32-furnace-thermostat`
Expected: Compiles.

**Step 6: Commit**

```
git commit -m "refactor: display config uses shared base_topic + MAC"
```

---

### Task 6: Update display MQTT connect, subscribe, publish, and sensor output

Wire up all display MQTT operations to new topic paths. Key change: sensor data publishes under the display's own path instead of the controller's namespace.

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp`
  - MQTT connect/LWT (~line 1941)
  - Subscribe block (~lines 1957-1984)
  - State publishing (~lines 1330-1442)
  - Sensor publishing (currently publishes to controller's `sensor/{MAC}/temp_c`)
  - Message callback (~lines 1695-1852)
  - Announce publish (new)
  - Packed command publish — remove MAC suffix from topic (now implicit in device path)

**Step 1: Update MQTT connect**

- LWT: `self_topic_for("state/availability")`
- Client ID: auto-generated

**Step 2: Update subscriptions**

- Own cmd topics: `self_topic_for("cmd/reboot")`, etc.
- Own cfg topics: `self_topic_for("cfg/+/set")`
- Controller state: `controller_topic_for_mac("state/mode")`, etc.
- Device discovery: `device_topic_for("+", "announce")`
- Remove `cmd/mode`, `cmd/fan_mode`, `cmd/target_temp_c` subscriptions (controller is sole HVAC authority now)

**Step 3: Update publish calls**

- All `topic_for(...)` → `self_topic_for(...)`
- Sensor data: publish to `self_topic_for("sensor/temp_c")` and `self_topic_for("sensor/humidity")` instead of controller's namespace
- Packed command: publish to `self_topic_for("state/packed_command")` (no MAC suffix needed)

**Step 4: Publish announce on connect**

```json
{"role":"display","firmware":"v0.9.0","name":"display-55D0E8"}
```
The `name` field uses `g_cfg_device_name`.

**Step 5: Guard HA discovery**

Wrap `mqtt_publish_discovery()` with `if (g_cfg_ha_discovery_enabled)`.

**Step 6: Build and verify**

Run: `pio run -e esp32-furnace-thermostat`
Expected: Compiles.

**Step 7: Commit**

```
git commit -m "refactor: display MQTT uses new topic paths, sensors publish under own device"
```

---

### Task 7: Update display HA discovery payloads

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp` — `mqtt_publish_discovery()` (~lines 1445-1640)

**Step 1: Update discovery payloads**

All `stat_t`, `cmd_t` references change from old base topic to `self_topic_for(...)`.

Display-specific entities (indoor temp sensor, humidity sensor, display timeout, backlight settings, reboot button) all reference the display's own device path.

**Step 2: Build and verify**

Run: `pio run -e esp32-furnace-thermostat`

**Step 3: Commit**

```
git commit -m "refactor: display HA discovery payloads use new device topic paths"
```

---

### Task 8: Update simulators

Both simulators use hardcoded topic constants. Update to match the new structure.

**Files:**
- Modify: `src/sim/controller_preview.cpp` (~lines 30-34, 62-68, all topic references)
- Modify: `src/sim/thermostat_ui_preview.cpp` (~lines 37-44, 141-147, all topic references)

**Step 1: Update controller simulator**

Replace hardcoded constants:
```cpp
// OLD:
static const std::string kControllerBaseTopic = "thermostat/furnace-controller";
static const std::string kDisplayBaseTopic = "thermostat/furnace-display";

// NEW:
static const std::string kBaseTopic = "esp32-wireless-thermostat";
static const std::string kControllerMac = "SIM_CTRL";
static const std::string kDisplayMac = "SIM_DISP";
```

Update `ctrl_topic()` and `display_topic()` to build `{base}/devices/{mac}/{suffix}`.

**Step 2: Update thermostat simulator**

Same pattern — update constants and topic helpers.

**Step 3: Build both simulators**

Run: `pio run -e native-controller-preview && pio run -e native-ui-preview`
Expected: Both compile.

**Step 4: Commit**

```
git commit -m "refactor: simulators use new MQTT topic structure"
```

---

### Task 9: Update integration and smoke test scripts

**Files:**
- Modify: `scripts/mqtt_path_smoke.py` — update topic paths
- Modify: `scripts/integration-test.sh` — no MQTT topic changes needed (HTTP only), but verify
- Create: `scripts/mqtt_cleanup_old_topics.py` — cleanup script for old retained messages

**Step 1: Update smoke test**

Replace:
```python
ctrl_base = f"{args.prefix}/furnace-controller"
disp_base = f"{args.prefix}/furnace-display"
```
With MAC-based paths. Add `--ctrl-mac` and `--disp-mac` CLI arguments. Default topic prefix becomes `esp32-wireless-thermostat`.

Update all topic references to `{prefix}/devices/{mac}/...` format.

**Step 2: Create cleanup script**

`scripts/mqtt_cleanup_old_topics.py` subscribes to old topic prefixes (`thermostat/furnace-controller/#`, `thermostat/furnace-display/#`, `wireless_thermostat_system/#`), collects retained messages, and publishes empty retained to each to clear them.

**Step 3: Run smoke test against running devices**

Run: `python3 scripts/mqtt_path_smoke.py --host 10.0.1.175 --ctrl-mac AABBCC --disp-mac 55D0E8`
Expected: PASS

**Step 4: Commit**

```
git commit -m "refactor: update test scripts for new MQTT topic structure, add cleanup script"
```

---

### Task 10: Update documentation

**Files:**
- Modify: `docs/deployment-runbook.md` — topic contract, config keys, smoke test usage
- Modify: `docs/plans/2026-03-09-mqtt-topic-redesign.md` — mark as implemented
- Modify: `CLAUDE.md` — update shared header references if needed

**Step 1: Update deployment runbook**

- Replace all old topic examples with new `{base_topic}/devices/{MAC}/...` format
- Update config contract tables (remove old keys, add new ones)
- Update smoke test usage with `--ctrl-mac` / `--disp-mac` args
- Document the cleanup script for migration

**Step 2: Commit**

```
git commit -m "docs: update deployment runbook for new MQTT topic structure"
```

---

### Task 11: End-to-end verification

**No code changes.** Run the full test suite against real hardware.

**Step 1: Run unit tests**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: All 37+ tests pass.

**Step 2: Build and flash both devices**

Build controller and display firmware. Flash via OTA or serial.

**Step 3: Run integration tests**

Run: `bash scripts/integration-test.sh --verbose`
Expected: 16/16 pass.

**Step 4: Run MQTT smoke test**

Run: `python3 scripts/mqtt_path_smoke.py --host 10.0.1.175 --ctrl-mac <MAC> --disp-mac <MAC>`
Expected: PASS.

**Step 5: Run cleanup script on old topics**

Run: `python3 scripts/mqtt_cleanup_old_topics.py --host 10.0.1.175`
Expected: Old retained messages cleared.

**Step 6: Verify HA discovery**

Check Home Assistant — climate entity, lockout switch, sensors should all appear and be controllable.

**Step 7: Commit any fixes, tag release**

```
git tag v1.0.0
```
