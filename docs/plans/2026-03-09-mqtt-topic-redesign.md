# MQTT Topic Redesign

## Problem

The current MQTT structure splits topics across three unrelated prefixes:
- `thermostat/furnace-controller` — controller state/cmd/cfg
- `thermostat/furnace-display` — display state/cmd/cfg
- `wireless_thermostat_system/devices` — peer discovery

Devices reference each other through hardcoded base topic config keys
(`display_mqtt_base_topic`, `controller_base_topic`). This is fragile,
requires manual configuration, and scatters related data across the broker.

## Design

### Unified Topic Hierarchy

All topics move under a single configurable base topic (default
`esp32-wireless-thermostat`). Each device publishes and subscribes under
its own MAC-based path:

```
{base_topic}/devices/{MAC}/
  announce              ← role, firmware version (retained)
  state/{key}           ← runtime state (retained, except packed_command)
  cfg/{key}/state       ← config values (retained)
  cfg/{key}/set         ← config updates (subscribed)
  cmd/{command}         ← commands (subscribed)
  sensor/{measurement}  ← sensor readings
```

### Controller Topics (`devices/{ctrl_MAC}/`)

Commands (subscribed):
- `cmd/mode` — "off", "heat", "cool"
- `cmd/fan_mode` — "auto", "on", "circulate"
- `cmd/target_temp_c` — float
- `cmd/lockout` — "0" or "1"
- `cmd/packed_word` — uint32 decimal string
- `cmd/sync` — "1"
- `cmd/reboot` — "1"
- `cmd/reset_sequence` — "1"
- `cmd/filter_reset` — "1"
- `cmd/primary_sensor_mac` — MAC string
- `cmd/espnow_only` — "0" or "1"

State (published, retained):
- `state/availability` — "online" (LWT: "offline")
- `state/mode`, `state/fan_mode`, `state/target_temp_c`
- `state/current_temp_c`, `state/current_humidity`
- `state/furnace_state` — "Idle", "Heating", "Cooling", etc.
- `state/lockout` — "0" or "1"
- `state/relay_heat`, `state/relay_cool`, `state/relay_fan` — "ON"/"OFF"
- `state/filter_runtime_hours`, `state/filter_change_required`
- `state/max_runtime_exceeded`
- `state/outdoor_temp_c`, `state/outdoor_condition`
- `state/firmware_version`, `state/boot_count`, `state/reset_reason`
- `state/uptime_s`, `state/free_heap_bytes`, `state/wifi_rssi`
- `state/last_mqtt_command_ms`, `state/last_espnow_rx_ms`
- `state/espnow_send_ok_count`, `state/espnow_send_fail_count`
- `state/error_mqtt`, `state/error_ota`, `state/error_espnow`
- `state/allow_ha`, `state/mqtt_enabled`, `state/espnow_enabled`
- `state/audit` — not retained

### Display Topics (`devices/{disp_MAC}/`)

Commands (subscribed):
- `cmd/reboot` — "1"
- `cmd/reset_sequence` — "1"
- `cmd/display_timeout_s` — integer
- `cmd/backlight_active_pct` — integer
- `cmd/backlight_screensaver_pct` — integer
- `cmd/temp_comp_c` — float
- `cmd/unit` — "c" or "f"

State (published, retained unless noted):
- `state/availability` — "online" (LWT: "offline")
- `state/packed_command` — uint32 (NOT retained)
- `state/command_seq` — integer (NOT retained)
- `state/mode`, `state/fan_mode`, `state/target_temp_c` — mirror of local UI
- `state/current_temp_c`, `state/current_humidity`
- `state/temp_comp_c`, `state/display_timeout_s`
- `state/backlight_active_pct`, `state/backlight_screensaver_pct`
- `state/firmware_version`, `state/boot_count`, `state/reset_reason`
- `state/uptime_s`, `state/free_heap_bytes`, `state/wifi_rssi`
- `state/wifi_ip`, `state/wifi_mac`, `state/wifi_ssid`, `state/wifi_channel`
- `state/connection_path`, `state/status`
- `state/last_mqtt_command_ms`, `state/last_espnow_rx_ms`
- `state/espnow_send_ok_count`, `state/espnow_send_fail_count`
- `state/error_mqtt`, `state/error_ota`, `state/error_espnow`

Sensor data (published):
- `sensor/temp_c` — float
- `sensor/humidity` — float

### Device Discovery & Authorization

**Announce:** Each device publishes a retained message on connect to
`{base_topic}/devices/{MAC}/announce` containing role (`controller`,
`display`, `sensor`), firmware version, and configurable device name
(from `device_name` config key).

**Controller authorization:**
- Maintains a list of authorized device MACs (existing `devices` config)
- Only accepts packed commands and sensor data from authorized MACs
- If the authorized list is empty, auto-registers the first device
  discovered via MQTT announce or ESP-NOW
- Unknown devices are ignored once any device is registered

**Display pairing:**
- Subscribes to `{base_topic}/devices/+/announce`, filters for
  `role=controller`
- Existing UI lets user select which controller to pair with
- Stores selected controller MAC in config; constructs controller topic
  path as `{base_topic}/devices/{ctrl_MAC}/...`

**Cross-device topic construction:** Both devices derive peer paths from
`{base_topic}/devices/{peer_MAC}/...`. The separate `display_mqtt_base_topic`
and `controller_base_topic` config keys are removed.

### Home Assistant Discovery

- Stays under `homeassistant/` prefix (required by HA)
- All `state_topic`, `command_topic` references in discovery payloads
  point to new `{base_topic}/devices/{MAC}/...` paths
- New config key `ha_discovery_enabled` (boolean, default `true`) to
  toggle HA discovery on/off
- `discovery_prefix` config key retained (default `homeassistant`)

### Config Key Changes

**Removed:**
- `mqtt_base_topic` — replaced by `base_topic` + device MAC
- `display_mqtt_base_topic` — replaced by peer MAC discovery
- `controller_base_topic` — replaced by peer MAC discovery
- `mqtt_client_id` — auto-generated as `{base_topic}-{MAC}`

**New:**
- `base_topic` — default `esp32-wireless-thermostat`
- `ha_discovery_enabled` — boolean, default `true`
- `device_name` — string, default auto-generated as `{role}-{last3MAC}`
  (e.g. `controller-29A9C4`, `display-55D0E8`). Used in announce payload
  and HA discovery as device display name.

**Unchanged:** WiFi, MQTT broker host/port/user/password, ESP-NOW,
OTA, weather, display settings, `discovery_prefix`, `devices`.

### What Doesn't Change

- ESP-NOW protocol (MQTT-only change)
- Packed command 24-bit word encoding
- Per-MAC sequence tracking
- Web server endpoints (`/status`, `/config`, `/update`, `/screenshot`)
- Failsafe / isolation reboot logic (monitors new paths)
- OTA rollback mechanism

### Migration

Clean break. New firmware uses only new topics. A cleanup script
(`scripts/mqtt_cleanup_old_topics.py`) clears retained messages from
the old `thermostat/furnace-controller/...`, `thermostat/furnace-display/...`,
and `wireless_thermostat_system/...` paths.
