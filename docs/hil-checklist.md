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

## ESP-NOW fallback during WiFi outages (2026-10-13 design)

Setup: bench controller on `esp32-furnace-controller-test`, spare display on
`thermostat-bench-normal`, both paired by MAC with the test LMK, both joined to WiFi and
the test broker. Watch both serial logs and both `/status` pages. Never use the
production devices for these scenarios.

- [ ] **F0 Probe spike (run before F1).** Confirms the channel probe's premise: a unicast
      `esp_now_send` is acknowledged only on the peer's channel. Build
      `SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF [SPIKE_LMK=<32 hex>] pio run -e espnow-probe-spike`
      (`src/bench/espnow_probe_spike.cpp`, env in `platformio.ini`) and flash it on the
      spare display. It cycles channels 1, 6 and 11 and prints per-send `status` and
      `latency_us`.
      - Case A: peer is the bench controller in ESP-NOW-only mode, parked on its channel.
        `status=1` only on that channel.
      - Case B: peer joined to WiFi. `status=1` only on its AP channel.
      - Case C: case A or B with encryption: build with the bench LMK
        `f0e1d2c3b4a596870718293a4b5c6d7e` and add the spike MAC to the controller's
        device list. Same selectivity; note whether the controller's `[espnow] rx=`
        counter increases.
      - Pass: all three show `status=1` only on the peer's channel and max latency under
        100 ms. Timed-out sends print `latency_us=0` with `status=-1`; tally those
        separately. If results look odd, add a short settle delay after
        `esp_wifi_set_channel`. Record the result in the spec under "Assumption to verify
        first" (`docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md`).
        If it fails, redesign the channel probe to the spec's dwell fallback before any
        production flash.

- [ ] **F1 Baseline.** Both on WiFi, same AP. `/status` on both: `espnow_connected` true,
      `connection_path` `mqtt+esp-now`, `espnow_search_count` 0.
- [ ] **F2 Both lose WiFi.** Block both clients in UniFi. Within 60 s both report
      `espnow_connected` true again (`[espnow] controller CONNECTED` on serial), on the
      rendezvous channel (11 in the test builds, 6 in production). Change the setpoint on
      the display: the controller relays follow. No reboot in 9 minutes.
- [ ] **F3 Controller alone loses WiFi.** Unblock both, then block only the controller.
      Within 60 s the controller's serial shows the peer linked on the display's AP
      channel; display commands still move the relays.
- [ ] **F4 Display alone loses WiFi.** Mirror of F3.
- [ ] **F5 Broker down, different AP channels.** Lock the two devices to APs on different
      2.4 GHz channels in UniFi. Stop the test broker. The display suspends WiFi only
      after 60 s associated with MQTT down, so within about 2 minutes it logs a WiFi
      suspend, links over ESP-NOW, and display commands move the relays. Start the
      broker: within 6 minutes the display is back on WiFi and MQTT.
- [ ] **F6 Furnace failsafe not tripped.** During F2-F5 the controller's `failsafe_active`
      stays false.
- [ ] **F7 Reboot ladder.** Power off the display, block the controller in UniFi, leave it
      24 h. Unblock. `net_reboot_count` is 3 or less; `boot_count` rose by the same
      amount; reboot reasons read `isolation: mqtt+espnow down, net_reboot=N`. After 10
      healthy minutes `net_reboot_count` returns to 0.
- [ ] **F8 In-place recovery.** At the start of F7, the controller's audit log shows
      `isolation: recovered in place` 2 minutes after isolation begins. On the classic
      board confirm WiFi is actually restored afterwards (ESP-NOW is deinitialised before
      Wi-Fi is stopped).
- [ ] **F9 Cold boot in an outage.** With both blocked in UniFi, power-cycle both. They
      link over ESP-NOW within 60 s and neither reboots in 30 minutes.
- [ ] **F10 Waveshare.** Repeat F2 and F7 with the Waveshare controller, unplugging
      Ethernet instead of blocking WiFi.
- [ ] **F11 Disconnect reason.** After F3, once the controller is unblocked,
      `wifi_disconnect_reason` on `/status` is non-zero.

Bench watch-items:
- arduino-esp32 makes one automatic reconnect after the first disconnect since boot, and
  a tracked join is capped at 10 s (the link manager pauses during it). Channel moves
  during a join show as `set_channel` failure logs.
- Classic board in-place recovery (F8) must really restore WiFi, not only ESP-NOW.
- While the display is suspended and linked over ESP-NOW, the 5-minute ResumeWifi cycle
  costs a link gap each cycle when the AP channel differs from the controller's.
- `error_espnow` reports send failures in the last 5 minutes and clears itself.

## Result
- Status: PASS / FAIL
- Notes:
