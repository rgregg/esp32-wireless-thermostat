# ESP-NOW Fallback During WiFi Outages — Design Spec

**Date:** 2026-10-13
**Branch:** `feat/espnow-wifi-outage-fallback`
**Status:** DRAFT for review. Touches the live, shared controller and display firmware.

## Goal

When WiFi (or the MQTT broker) is unavailable, the controller and the display keep
working together over ESP-NOW, so the wall display can still change mode and setpoint
and the furnace keeps heating or cooling. Home Assistant control is expected to be lost
until the network returns.

A long outage must not produce a reboot loop.

## Confirmed decisions

| Decision | Choice |
|---|---|
| Target hardware | Both controller boards: classic `esp32dev` (WiFi) and Waveshare (Ethernet, radio never associated), plus the ESP32-S3 display |
| Channel agreement | The device that is not associated to WiFi follows its peer |
| Channels searched | Only the non-overlapping 2.4 GHz channels 1, 6 and 11 (plus the last channel where the peer was heard) |
| Reboot policy | In-place recovery first, then backed-off reboots capped at one per 24 h |

## Background: the 2026-10-13 incident

The production classic controller's WiFi went silent at 08:52:40 PDT. MQTT keepalives
and ESP-NOW heartbeats stopped in the same second, the AP logged a disconnect, and the
controller did not re-associate. It recovered only when the isolation watchdog rebooted
it 31 minutes later (`isolation: mqtt+espnow down 900s`). The furnace failsafe turned
the relays off after 5 minutes, so HVAC was off for about 26 minutes.

What the investigation found in the current firmware:

1. **ESP-NOW works only when both radios share a channel.** While a device is
   associated, its channel is the AP's; the configured `espnow_channel` (6) is ignored.
   The site's APs use channels 1, 6 and 11 and the controller has roamed between four of
   them since August. When the two devices sit on different channels the fallback is
   silently dead.
2. **Nothing restores a common channel when WiFi drops.** `WifiProvisioningManager`
   calls `WiFi.begin()` every 5 s, which scans, and neither device returns to an agreed
   channel.
3. **Isolation reboots repeat forever.** Both devices reboot after 15 minutes with MQTT
   and ESP-NOW down, with no limit across reboots. `wifi_watchdog.h` adds a second
   5-minute reboot path when the gateway is unreachable.
4. **`wifi_watchdog_tick` returns early when `WiFi.status() != WL_CONNECTED`**, so a
   lost association that never comes back is not handled by it at all.
5. **The controller's "ESP-NOW heartbeat" is not ESP-NOW specific.**
   `heartbeat_last_seen_ms` is also refreshed by MQTT messages from the display
   (`state/availability`, `state/packed_command`, `sensor/temp_c`). The isolation check
   treats it as ESP-NOW liveness for 15 minutes after the last update, which is why the
   incident had a dead 15-minute wait before the isolation clock even started.

## Design

### 1. Channel agreement — `EspNowChannelPolicy`

A new platform-agnostic, natively unit-tested policy in `include/transport/` and
`src/transport/`, in the style of `NetworkRecoveryPolicy`. It makes no radio calls. Each
tick the caller passes in:

- `now_ms`
- role (controller or display)
- whether the STA is associated, and the WiFi channel if so
- whether MQTT is up
- `last_peer_rx_ms`: last authorized ESP-NOW frame from the peer (true ESP-NOW receive)
- result of the last probe (acknowledged or not), when one was requested

It returns one action: `None`, `SetChannel(n)`, `Probe` (send a unicast heartbeat and
report the send status), or, display only, `SuspendWifi` / `ResumeWifi`.

The peer counts as **linked** when `last_peer_rx_ms` is within 30 s (three heartbeat
intervals).

Rules:

- **Associated:** the channel is the AP's. No channel actions.
- **Not associated, linked:** stay parked on the current channel.
- **Not associated, not linked:** search. Candidate order is the last channel where the
  peer was heard, then the configured rendezvous channel (`espnow_channel`, default 6),
  then the remaining channels of {1, 6, 11}. On each candidate: `SetChannel`, `Probe`,
  and park there if the probe is acknowledged at the radio level. One pass covers at
  most four channels.
- **Neither device associated:** to stop the two chasing each other, the controller
  parks on the rendezvous channel and makes one search pass every 20 s; the display
  makes a search pass every 5 s. The display's pass always includes the rendezvous
  channel, so they converge there.
- **Broker down, WiFi up, channel mismatch (display only):** if the display is
  associated, MQTT has been down for 60 s and the peer is not linked, it returns
  `SuspendWifi`. The display disconnects, searches as above, and returns `ResumeWifi`
  every 5 minutes to retry WiFi and MQTT. If MQTT comes back it stays on WiFi.

On Waveshare the controller is never associated, so the same rules make it follow the
display's AP channel while the display is on WiFi, and park on the rendezvous channel
when the display is not.

The search is limited to channels 1, 6 and 11 by decision. A peer associated to an AP on
any other channel is reachable only if it was last heard there. The diagnostics in
section 5 make that condition visible.

**Assumption to verify first:** that a unicast `esp_now_send` to a peer on the same
channel reports success through the send callback, and reports failure on a different
channel, quickly enough to use as a probe, including when the peer is associated and
when encryption is enabled. This is the first implementation task (bench spike). If it
does not hold, the fallback is to dwell 12 s per candidate channel listening for the
10 s heartbeat, about 36 s for a full pass.

Bench result (scenario F0 in `docs/hil-checklist.md`, 2026-10-20): **holds.** About 119
probes per channel per case on channels 1, 6 and 11. Peer on the probed channel: 97.5-100 %
acknowledged, worst-case send-callback latency 42.7 ms (typically 2-3 ms). Peer on another
channel: 0 acknowledged in every case except 3 of 119 while an unlinked ESP-NOW-only peer
was moving channels mid-run. Held for an ESP-NOW-only peer (Waveshare) and for a peer
associated to an AP in default modem sleep (ESP32-S3 WiFi controller), with and without
encryption. No send timed out. No modem-sleep change or multi-frame probe is needed.

Finding: a probe is acknowledged at the MAC layer even when the peer then drops the frame
for a key mismatch (spike with a wrong LMK: 100 % acknowledged on the peer's channel, peer
receive counter unchanged). So a probe proves only that a radio with the peer's MAC is on
that channel, not that the link works. That is enough for channel selection (a unicast ack
comes only from the peer's own MAC), and link state already comes from received, decrypted
frames (`espnow_connected`), so a mis-keyed peer is found but never reported linked.

### 2. WiFi rejoin behaviour

- `WifiProvisioningManager::ensure_connected` gets a variable retry interval: 5 s as
  today while there is no ESP-NOW link, 60 s while the peer is linked over ESP-NOW, so
  join scans stop interrupting a working link. After each failed join the caller
  restores the parked ESP-NOW channel.
- `WiFi.setAutoReconnect(false)`; the manager owns every join attempt, so the driver
  does not scan on its own.
- The display honours `SuspendWifi` / `ResumeWifi` by skipping or re-enabling
  `ensure_connected`.
- `wifi_watchdog_tick` no longer owns any reboot. It reports "gateway unreachable" and
  "association lost" durations to the reboot governor (section 3). Its forced
  disconnect/reconnect at 2 minutes stays.

### 3. Reboot governor — `NetworkRebootGovernor`

A second platform-agnostic, natively unit-tested policy shared by both devices. Every
network-triggered reboot (isolation, WiFi watchdog) goes through it. Non-network reboots
(OTA, user request, weather wedge, task watchdog) are unchanged.

A device is **isolated** when MQTT is down and the ESP-NOW peer is not linked, measured
from the moment both are down, using the true ESP-NOW receive time. The existing
`s_network_was_ready` gate on the controller stays.

| Time isolated | Action |
|---|---|
| 2 min | Recover in place: re-initialise ESP-NOW (`transport.restart()`), stop/start WiFi where the board uses it, force an MQTT reconnect |
| 10 min | Reboot 1 |
| 1 h after reboot 1 | Reboot 2 |
| 4 h after reboot 2 | Reboot 3 |
| every 24 h after that | One reboot |

- The consecutive network-reboot count is stored in NVS, written only when a reboot is
  taken. After a network reboot the device waits the next interval before rebooting
  again, counted from boot.
- The count resets to zero once MQTT or the ESP-NOW link has been continuously healthy
  for 10 minutes.
- In-place recovery, currently Waveshare-only, runs on the classic controller and the
  display too. On the classic board it happens only when the device is already isolated,
  so there is no working association to disrupt.
- The reboot reason is persisted as today, with the count appended.

Worst case in the first 24 hours of a total outage: 3 reboots (at 10 min, 1 h 10 min and 5 h 10 min), then one per day.

### 4. Operating on ESP-NOW alone

No new behaviour. Already in place: the display sends command words, indoor temperature
and humidity over ESP-NOW; the controller sends telemetry, weather and time; the
controller accepts ESP-NOW commands whenever MQTT is not the active primary. The 5-minute
furnace failsafe is unchanged and should not trip once the link recovers in seconds.

One required correction: the controller gains a transport-level
`last_peer_rx_ms` (set in `EspNowControllerTransport::on_recv` after the peer filter),
and the display gets the same. The policies in sections 1 and 3 use this value. The
existing `heartbeat_last_seen_ms`, fed by both MQTT and ESP-NOW, keeps driving the
failsafe.

### 5. Diagnostics

Published over MQTT with Home Assistant discovery, and shown on each device's `/status`:

- current ESP-NOW channel
- link path (`mqtt`, `esp-now`, `mqtt+esp-now`, `none`)
- channel search count
- consecutive network-reboot count and seconds until the next reboot is allowed
- last WiFi disconnect reason code (from the WiFi event), also written to the audit log

`error_espnow` becomes a recent-window value (failures in the last 5 minutes) instead of
latching `send_failed` on the first failure since boot.

### 6. Testing

Native unit tests (`native-tests`):

- `EspNowChannelPolicy`: each rule above, candidate ordering, controller/display
  asymmetry, suspend/resume timing, `millis()` wraparound.
- `NetworkRebootGovernor`: the ladder, persistence across simulated reboots, reset after
  10 healthy minutes, no reboot before network-ready.

Bench (spare display `80B54ED1B804`, bench controller, isolated test broker):

1. Probe spike from section 1.
2. AP off for both devices: link over ESP-NOW within 30 s; display commands move relays.
3. One device blocked in UniFi while the other stays associated: blocked device finds the
   peer's channel.
4. Broker stopped with the devices forced onto APs on different channels: display
   suspends WiFi, links, and resumes when the broker returns.
5. Peer powered off with WiFi off for 24 h: no more than 3 reboots, count visible after
   WiFi returns.
6. Repeat 2 and 5 on the Waveshare controller with Ethernet unplugged.

### 7. Rollout

Display and controller firmware must be updated together, since the search relies on
both sides. Flash production only after the bench list passes and with explicit
go-ahead. Either device running old firmware degrades to today's behaviour, not worse.

## Out of scope

- Root cause of the classic controller's radio going silent. The disconnect reason code
  from section 5 is the evidence needed next time.
- Controller losing window mode and lockout on reboot (issue #28).
- ESP-NOW encryption key provisioning. The controller reports an LMK set and the display
  reports none; confirm during the bench spike which configuration is actually in effect.
