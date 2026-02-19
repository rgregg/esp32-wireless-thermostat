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
- Display local fallback config: `http://<display-ip>/config`
- Display screenshot: `http://<display-ip>/screenshot`

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

## mDNS Hostnames
- Controller advertises HTTP using OTA hostname (default `esp32-furnace-controller`)
- Display advertises HTTP using OTA hostname (default `esp32-furnace-thermostat`)
