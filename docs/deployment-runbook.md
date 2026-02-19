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
- Health telemetry:
  - `state/boot_count`
  - `state/reset_reason`
  - `state/uptime_s`

## mDNS Hostnames
- Controller advertises HTTP using OTA hostname (default `esp32-furnace-controller`)
- Display advertises HTTP using OTA hostname (default `esp32-furnace-thermostat`)
