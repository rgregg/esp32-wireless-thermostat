# Hardware-In-Loop Checklist

Use this checklist after flashing both devices.

## Metadata
- Date:
- Controller firmware version:
- Display firmware version:
- Controller board ID:
- Display board ID:
- WiFi SSID/channel:

## Controller Checks
- [ ] Relays boot OFF (GPIO32/33/25/26).
- [ ] `cmd/lockout=1` forces all relays OFF and keeps them OFF.
- [ ] `cmd/lockout=0` re-enables normal relay behavior.
- [ ] Heat demand drives only heat relay.
- [ ] Cool demand drives only cool relay.
- [ ] Fan-only demand drives only fan relay.
- [ ] Relay transition honors interlock wait timing (heat 0.5s, others 1s).
- [ ] Failsafe timeout forces relays OFF.

## Thermostat Checks
- [ ] Display boots and touch is responsive.
- [ ] AHT20 indoor temperature/humidity updates every minute.
- [ ] Local setpoint/mode/fan changes are reflected on controller state.
- [ ] Display timeout setting changes idle timing behavior.
- [ ] Clock shows real local time (not uptime counter).
- [ ] Settings diagnostics show IP/MAC/SSID/channel/RSSI/firmware.
- [ ] Weather/outdoor values update from PirateWeather API using configured API key + ZIP.

## Transport Checks
- [ ] ESP-NOW path works when MQTT is unavailable.
- [ ] MQTT path updates controller state from display packed command mirror (`state/packed_command`).
- [ ] While MQTT-primary is active, ESP-NOW commands are gated on controller.
- [ ] After MQTT outage, ESP-NOW fallback takes over.
- [ ] After MQTT restore, controller returns to MQTT-primary mode.

## Home Assistant Checks
- [ ] Climate entity available and controllable.
- [ ] Lockout switch entity available and controllable.
- [ ] Filter runtime and furnace state sensors update.
- [ ] Device model appears as one logical device identifier.

## OTA Checks
- [ ] Web upload form loads at `http://<controller-ip>/update`.
- [ ] Web upload form loads at `http://<display-ip>/update`.
- [ ] Firmware upload via web form succeeds and device reboots.
- [ ] After successful OTA, device confirms health and stays on new firmware.
- [ ] Flashing intentionally bad firmware triggers automatic rollback to previous image.

## Result
- Status: PASS / FAIL
- Notes:
