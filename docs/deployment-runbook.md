# Deployment Runbook

## Defaults
- MQTT host defaults to `mqtt.lan` on both devices.
- MQTT username/password default to empty.
- ESP-NOW default channel is `6` on both devices.
- ESP-NOW default LMK is set on both devices (same 16-byte key in hex).
- ESP-NOW peer MAC default is broadcast (`FF:FF:FF:FF:FF:FF`), which keeps multicast fallback.
  - Note: ESP-NOW encryption requires a unicast peer MAC; with broadcast peer, traffic is unencrypted.
  - To enforce allowed-device communication and encrypted ESP-NOW traffic, set `espnow_peer_mac` to the other device's MAC on both devices.

## First Boot
1. Flash controller and display firmware.
2. Join both devices to WiFi.
3. Confirm controller connects to MQTT at `mqtt.lan:1883`.
4. Open controller web UI (`http://<controller-ip>/`) for unified settings.
5. Verify display status appears in the controller UI and apply display settings there.

## Runtime Management
- Controller local config: `http://<controller-ip>/config`
- Controller reboot endpoint: `POST http://<controller-ip>/reboot`
- Controller web OTA upload: `http://<controller-ip>/update`
- Display local fallback config: `http://<display-ip>/config`
- Display reboot endpoint: `POST http://<display-ip>/reboot`
- Display web OTA upload: `http://<display-ip>/update`
- Display screenshot: `http://<display-ip>/screenshot`
- MQTT path smoke verifier (with broker + both devices online):
  - `python3 scripts/mqtt_path_smoke.py --host mqtt.lan`
- MQTT reboot commands:
  - Controller: publish `1` to `thermostat/furnace-controller/cmd/reboot`
  - Display: publish `1` to `thermostat/furnace-display/cmd/reboot`
- Sequence recovery command (no reboot required):
  - Publish `1` to `thermostat/furnace-controller/cmd/reset_sequence`
  - Controller resets its command sequence filter and forwards reset to display.

## Release Artifacts
- Build both firmware binaries with one command:
  - `scripts/build_release_artifacts.sh`
- Artifacts are written to `dist/releases/<git-describe-version>/`.
- Naming format:
  - `esp32-furnace-controller-<version>.bin`
  - `esp32-furnace-thermostat-<version>.bin`
- Build metadata is recorded in `build-info.txt`, and `SHA256SUMS` is generated when `shasum` is available.
- The GitHub Releases workflow automatically attaches firmware binaries to tagged releases.

## Runtime Config Contract

### Controller keys (`thermostat/furnace-controller/cfg/<key>/set`)
| Key | Type | Default | Accepted values/range | Reboot required |
| --- | --- | --- | --- | --- |
| `wifi_ssid` | string | `""` | any | no |
| `wifi_password` | string (secret) | `""` | any | no |
| `mqtt_host` | string | `mqtt.lan` | any hostname/IP | no |
| `mqtt_port` | integer | `1883` | `1..65535` | no |
| `mqtt_user` | string | `""` | any | no |
| `mqtt_password` | string (secret) | `""` | any | no |
| `mqtt_client_id` | string | `esp32-furnace-controller` | any | no |
| `mqtt_base_topic` | string | `thermostat/furnace-controller` | any | no |
| `display_mqtt_base_topic` | string | `thermostat/furnace-display` | any | no |
| `shared_device_id` | string | `wireless_thermostat_system` | any | no |
| `ota_hostname` | string | `esp32-furnace-controller` | any | no |
| `ota_password` | string (secret) | `""` | any | no |
| `espnow_channel` | integer | `6` | `1..14` | yes |
| `espnow_peer_mac` | string | `FF:FF:FF:FF:FF:FF` | MAC format expected | yes |
| `espnow_lmk` | string (secret) | `a1b2c3d4e5f60718293a4b5c6d7e8f90` | 32 hex chars expected | yes |

### Thermostat keys (`thermostat/furnace-display/cfg/<key>/set`)
| Key | Type | Default | Accepted values/range | Reboot required |
| --- | --- | --- | --- | --- |
| `wifi_ssid` | string | `""` | any | no |
| `wifi_password` | string (secret) | `""` | any | no |
| `mqtt_host` | string | `mqtt.lan` | any hostname/IP | no |
| `mqtt_port` | integer | `1883` | `1..65535` | no |
| `mqtt_user` | string | `""` | any | no |
| `mqtt_password` | string (secret) | `""` | any | no |
| `mqtt_client_id` | string | `esp32-furnace-thermostat` | any | no |
| `mqtt_base_topic` | string | `thermostat/furnace-display` | any | no |
| `discovery_prefix` | string | `homeassistant` | any | no |
| `shared_device_id` | string | `wireless_thermostat_system` | any | no |
| `pirateweather_api_key` | string (secret) | `""` | any | no |
| `pirateweather_zip` | string | `""` | ZIP string | no |
| `display_timeout_s` | integer | `300` | clamped to `30..600` | no |
| `temp_comp_c` | float | `0.0` | any float | no |
| `temperature_unit` | string | `c` | `c`,`f`,`celsius`,`fahrenheit` | no |
| `ota_hostname` | string | `esp32-furnace-thermostat` | any | no |
| `ota_password` | string (secret) | `""` | any | no |
| `espnow_channel` | integer | `6` | `1..14` | yes |
| `espnow_peer_mac` | string | `FF:FF:FF:FF:FF:FF` | MAC format expected | yes |
| `espnow_lmk` | string (secret) | `a1b2c3d4e5f60718293a4b5c6d7e8f90` | 32 hex chars expected | yes |
| `controller_timeout_ms` | integer | `30000` | `1000..600000` | yes |

## Weather Provider (Display)
- Weather is sourced from PirateWeather API polling on the display firmware.
- Configure both of these display runtime config keys:
  - `pirateweather_api_key`
  - `pirateweather_zip` (US ZIP code)
- You can set display config either from:
  - Controller unified UI: `http://<controller-ip>/` (Display section, proxied over MQTT config topics)
  - Display local UI: `http://<display-ip>/`
- If API config is missing or fetches fail, display falls back to stub weather (`Cloudy`, `6.0C`).

## Discovery and Telemetry
- Controller base topic default: `thermostat/furnace-controller`
- Display base topic default: `thermostat/furnace-display`
- Protocol-aligned command transport over MQTT:
  - Thermostat publishes packed command word mirror: `thermostat/furnace-display/state/packed_command`
  - Controller can accept packed command word directly: `thermostat/furnace-controller/cmd/packed_word`
  - Home Assistant granular topics remain supported (`cmd/mode`, `cmd/fan_mode`, `cmd/target_temp_c`, etc.)
- Health telemetry:
  - `state/boot_count`
  - `state/reset_reason`
  - `state/uptime_s`
  - `state/wifi_rssi`
  - `state/free_heap_bytes`
  - `state/last_mqtt_command_ms`
  - `state/last_espnow_rx_ms`
  - `state/espnow_send_ok_count`
  - `state/espnow_send_fail_count`
- Error reason telemetry:
  - `state/error_mqtt` (`none` or `connect_state_<code>`)
  - `state/error_ota` (`none` or `ota_error_<code>`)
  - `state/error_espnow` (`none`, `send_failed`, or `begin_failed`)
- Home Assistant discovery now includes diagnostic sensors for both controller and display:
  - WiFi RSSI, free heap, last MQTT command timestamp, last ESP-NOW RX timestamp
  - ESP-NOW send OK/fail counters
  - MQTT/OTA/ESP-NOW last error reason

## mDNS Hostnames
- Controller advertises HTTP using OTA hostname (default `esp32-furnace-controller`)
- Display advertises HTTP using OTA hostname (default `esp32-furnace-thermostat`)

## OTA Rollout + Rollback

### OTA Methods
- **ArduinoOTA**: network OTA via PlatformIO or `espota`.
- **Web OTA**: browser-based upload form at `http://<device-ip>/update`.

### Automatic Rollback
New firmware boots in a **pending-verify** state. The device must establish WiFi and MQTT connectivity within 3 minutes to confirm the update. If the device crashes or fails to connect in time, the bootloader automatically reverts to the previous firmware image.

### Rollout Order
1. Verify both devices are online and healthy before update:
   - Controller `state/uptime_s` is advancing.
   - Display `state/uptime_s` is advancing.
   - Thermostat publishes `state/packed_command` and controller applies commands.
2. Update thermostat (display) first via OTA (ArduinoOTA or web upload at `/update`).
3. Wait for display to reconnect to WiFi + MQTT and resume telemetry.
4. Update controller second via OTA.
5. Re-run MQTT smoke verification:
   - `python3 scripts/mqtt_path_smoke.py --host mqtt.lan`

### Health Checks After Each Node Update
- Device responds on HTTP root and `/config`.
- Device reconnects to MQTT and republishes retained runtime state.
- For thermostat, `/screenshot` endpoint returns current UI frame.

### Rollback Triggers
- Automatic rollback fires if WiFi+MQTT are not established within 3 minutes of boot.
- Manual rollback warranted if:
  - Runtime state topics stop updating after reconnect.
  - Command path regression (packed or granular topics stop applying).
  - Web management endpoints become unavailable.

### Rollback Procedure
1. In most cases, automatic rollback handles failed updates without intervention.
2. If manual rollback is needed, re-flash previous known-good firmware using OTA if reachable.
3. If OTA is unavailable, use serial recovery on bench hardware, then redeploy.
4. After rollback, verify:
   - retained cfg state is intact
   - command path works (`mqtt_path_smoke.py`)
   - Home Assistant entities report expected state

### One Node Unreachable Recovery
- If thermostat is unreachable:
  - keep controller on known-good build
  - recover thermostat first (OTA if possible, serial otherwise)
  - run MQTT smoke and screenshot checks before controller changes
- If controller is unreachable:
  - recover controller first (it hosts unified management UI)
  - confirm display reconnects and cfg proxy state repopulates
