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
      - Production radio settings: the firmware never calls `WiFi.setSleep(false)`, so a
        peer joined to WiFi runs in the default modem sleep. Run cases B and C with the
        peer associated in that default (no sleep change on either side).
      - At least 100 probes per case. Record the ack RATE (acked / sent) on the peer's
        channel and on each of the other channels, not only whether any ack arrived.
      - Encrypted sender to unencrypted receiver: build the spike with an LMK while the
        receiving peer has no key for it. Record whether `status=1` still appears and
        whether the receiver's `[espnow] rx=` counter moves. A MAC-layer ACK can arrive
        even when ESP-NOW then drops the frame for a key mismatch; if so, probes are false
        positives against a mis-keyed peer. Note it in the spec.
      - If the ack rate on the correct channel is low (well under 100 %), the follow-ups
        are disabling modem sleep on the mains-powered devices or sending 2-3 frames per
        probe. Record which, if any, is needed before production.
      - Pass: all three show `status=1` only on the peer's channel and max latency under
        100 ms. Timed-out sends print `latency_us=0` with `status=-1`; tally those
        separately. If results look odd, add a short settle delay after
        `esp_wifi_set_channel`. Record the result in the spec under "Assumption to verify
        first" (`docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md`).
        If it fails, redesign the channel probe to the spec's dwell fallback before any
        production flash.

- [ ] **F1 Baseline.** Both on WiFi, same AP. `/status` on both: `espnow_connected` true,
      `espnow_search_count` 0. Controller `/status`: `connection_path` `mqtt+esp-now`. The
      display's `/status` has no `connection_path`; read its MQTT topic
      `state/connection_path` instead.
- [ ] **F2 Both lose WiFi.** Block both clients in UniFi. Within 60 s both report
      `espnow_connected` true again (`[espnow] controller CONNECTED` on serial), on the
      rendezvous channel (11 in the test builds, 6 in production). Change the setpoint on
      the display: the controller relays follow. No reboot in 9 minutes.
- [ ] **F3 Controller alone loses WiFi.** Unblock both, then block only the controller.
      Within 60 s the controller's serial shows the peer linked on the display's AP
      channel; display commands still move the relays.
- [ ] **F4 Display alone loses WiFi.** Mirror of F3.
- [ ] **F5 Broker down, different AP channels.** Lock the two devices to APs on different
      2.4 GHz channels in UniFi. Stop the test broker. The display suspends WiFi once it
      is associated and MQTT has been down for 60 s, so within about 2 minutes its
      `/status` shows `wifi_suspended` true (it also logs the suspend), it links over
      ESP-NOW, and display commands move the relays. Start the
      broker: within 6 minutes the display is back on WiFi and MQTT.
      - Timing: record when the display detects MQTT down versus the controller, and when
        the controller first logs `recovered in place` relative to the display's
        `[espnow] controller CONNECTED`. The controller should not recover in place before
        the display has linked; record it if it does.
- [ ] **F6 Furnace failsafe not tripped.** During F2-F5 the controller's `failsafe_active`
      stays false.
- [ ] **F7 Reboot ladder.** Power off the display, block the controller in UniFi, leave it
      24 h. Read `net_reboot_count` (NVS `net_rb_cnt`) after each reboot: 1 at 10 min, 2
      at 1 h 10 min, 3 at 5 h 10 min, and no more than 3 reboots in the first 24 h.
      Unblock. `boot_count` rose by the same amount; reboot reasons read
      `isolation: mqtt+espnow down, net_reboot=N`. After 10 healthy minutes
      `net_reboot_count` returns to 0.
- [ ] **F8 In-place recovery.** Run during the F7 setup (display off, controller blocked).
      Two minutes after isolation begins, the controller's audit log shows
      `isolation: recovered in place`, and again 30 minutes later (32 min) if the
      controller is still isolated and has not rebooted (needs `net_rb_cnt` of 1 or more,
      so the reboot is not due at 10 min). On the classic
      board confirm WiFi is actually restored afterwards (ESP-NOW is deinitialised before
      Wi-Fi is stopped).
      - Classic board, MQTT task mid-connect: trigger the recovery while the controller's
        MQTT task is inside a connect attempt (broker host reachable at IP level but not
        answering, e.g. a firewall drop on 1883). No task watchdog reset, and WiFi rejoins
        afterwards.
- [ ] **F9 Cold boot in an outage.** With both blocked in UniFi, power-cycle both. They
      link over ESP-NOW within 60 s and neither reboots in 30 minutes.
- [ ] **F10 Waveshare.** Repeat F2 and F7 with the Waveshare controller, unplugging
      Ethernet instead of blocking WiFi. For F7, read `net_reboot_count` after each reboot
      (no more than 3 reboots in the first 24 h).
- [ ] **F11 Disconnect reason.** After F3, once the controller is unblocked,
      `wifi_disconnect_reason` on `/status` is non-zero.
- [ ] **F12 AP up, DHCP/gateway down.** Use a test SSID with no DHCP server (or block the
      gateway for both clients), devices on APs with different channels, for more than 6
      minutes. Expected: each join that associates without an IP is dropped after 20 s
      (`[wifi] associated without IP for 20 s`) and retried after the normal interval, not
      re-bounced every 10 s; the display suspends WiFi (`wifi_suspended` true) and links
      over ESP-NOW; the controller searches and follows the display's channel between its
      joins; display commands move the relays; `failsafe_active` stays false.
      - If the display links over ESP-NOW between its own joins, the policy keeps it on
        WiFi (it never suspends while linked), so instead of suspending it cycles about
        60 s off WiFi and about 22 s associated. Record which happened, and confirm the
        link (`espnow_connected`) survives each associated stretch.
- [ ] **F13 Mixed versions.** New controller with the old display, then old controller
      with the new display. Run F2 and F5 with each pair: neither is worse than today
      (old firmware on both), the furnace failsafe does not trip, and no device reboots
      more often than before.

Bench watch-items:
- arduino-esp32 makes one automatic reconnect after the first disconnect since boot, and
  a tracked join is capped at 10 s until it associates (the link manager pauses during
  it), then 20 s associated without an IP (`[wifi] associated without IP for 20 s` is
  logged when one is dropped). Channel moves during a join show as `set_channel` failure
  logs.
- Classic board in-place recovery (F8) must really restore WiFi, not only ESP-NOW.
- While the display is suspended and linked over ESP-NOW, the 5-minute ResumeWifi cycle
  costs a link gap each cycle when the AP channel differs from the controller's.
- `error_espnow` reports send failures in the last 5 minutes and clears itself.
- MQTT down while ESP-NOW is linked (F5 left running past 60 minutes): the controller logs
  `mqtt down, espnow linked: recovered in place` every 60 minutes and never reboots; the
  display logs none while `wifi_suspended` is true.

## Pre-flash checks (production)

- [ ] The production controller's `devices` list has exactly one unicast peer (the
      display's MAC). The channel search assumes a single unicast peer.
- [ ] Both production devices show an advancing `state/last_espnow_rx_ms` over MQTT (the
      "Last ESP-NOW RX" sensors in Home Assistant), proving true ESP-NOW receive, before
      the fallback is relied on.

## Result
- Status: PASS / FAIL
- Notes:
