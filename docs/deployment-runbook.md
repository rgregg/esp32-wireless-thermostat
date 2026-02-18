# Deployment Runbook

## Defaults
- MQTT host defaults to `mqtt.lan` on both devices.
- MQTT username/password default to empty.
- ESP-NOW default channel is `6` on both devices.

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

## Discovery and Telemetry
- Controller base topic default: `thermostat/furnace-controller`
- Display base topic default: `thermostat/furnace-display`
- Health telemetry:
  - `state/boot_count`
  - `state/reset_reason`
  - `state/uptime_s`

## mDNS Hostnames
- Controller advertises HTTP using OTA hostname (default `furnace-controller`)
- Display advertises HTTP using OTA hostname (default `furnace-display`)

