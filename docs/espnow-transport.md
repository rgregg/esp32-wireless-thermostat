# ESP-NOW Transport

## Scope
This documents the current ESP-NOW transport adapters present in the PlatformIO port.

## Packet Types
Defined in `./include/transport/espnow_packets.h`.

- `Heartbeat` (`PacketType::Heartbeat`)
- `CommandWord` (`PacketType::CommandWord`)
- `ControllerTelemetry` (`PacketType::ControllerTelemetry`)
- `IndoorTemperature` (`PacketType::IndoorTemperature`)
- `IndoorHumidity` (`PacketType::IndoorHumidity`)

Protocol version: `1`.

## Implemented Adapters

Controller-side adapter:
- `./include/controller/transport/espnow_controller_transport.h`
- `./src/controller/transport/espnow_controller_transport.cpp`

Thermostat-side adapter:
- `./include/thermostat/transport/espnow_thermostat_transport.h`
- `./src/thermostat/transport/espnow_thermostat_transport.cpp`

## Current Behavior

Controller adapter:
- Initializes ESP-NOW and one configured peer.
- Receives heartbeat, command-word, indoor temperature, and indoor humidity packets.
- Sends periodic heartbeat and controller telemetry packets.

Thermostat adapter:
- Initializes ESP-NOW and one configured peer.
- Receives heartbeat and controller telemetry packets.
- Sends periodic heartbeat, packed command word, indoor temperature, and indoor humidity packets.

Both adapters include native-build-safe no-op behavior so host tests still compile.

## Integration Points
- `ControllerApp` owns control logic and emits telemetry via `IControllerTransport`.
- `EspNowControllerTransport` implements `IControllerTransport`.
- `ControllerNode` wires controller transport callbacks into `ControllerApp`.
- `ThermostatApp` owns thermostat-side command sequencing and controller telemetry ingestion.
- `ThermostatNode` wires thermostat transport callbacks into `ThermostatApp`.

## Channel Agreement and WiFi Outages

ESP-NOW only works when both radios are on the same channel. While a device is associated
to WiFi its channel is the access point's, so the configured `espnow_channel` applies
only when neither device is on WiFi. "Associated" here means associated with the AP at
L2 (`esp_wifi_sta_get_ap_info()` succeeds), with or without an IP: an STA waiting for
DHCP is already on the AP's channel, so the link manager never changes the channel,
probes or enforces while associated.

- A device that is **not** associated follows its peer. The peer counts as linked if an
  authorized frame arrived within 30 s. When it goes quiet, the device runs a search
  pass: it probes the channel where the peer was last heard, then `espnow_channel` (the
  rendezvous channel), then the rest of 1, 6 and 11. Only those channels (plus the last
  heard one) are searched. A pass starts every 20 s on the controller and every 5 s on the
  display; after an acknowledged probe the device holds that channel for 30 s awaiting a
  frame, and between passes it rests on the rendezvous channel.
- A probe is a unicast heartbeat sent on the candidate channel. It counts as acknowledged
  only if the ESP-NOW send callback reports success within 200 ms. Results are attributed
  by send sequence (`include/transport/espnow_probe_tracker.h`), so a late callback for an
  earlier frame never acknowledges a later probe. If a send callback is lost (no probe
  outstanding and completions behind issued sends for more than 1 s), the tracker resyncs
  so later probes are still attributed correctly.
- All ESP-NOW sends happen on the main loop task, which the probe attribution relies on.
  Command-triggered telemetry on the controller and the display's controller ACK are
  deferred to the loop tick rather than sent from the receive callback.
- The link manager pauses while a tracked WiFi join is scanning (`join_in_progress`,
  not yet associated): no channel change, probe or enforcement, and any outstanding probe
  is dropped. When a join ends unassociated, the radio is put back on the wanted channel
  (checked once a second). Tracking: `include/wifi_join_tracker.h`.
- A join is in progress from its start until GOT_IP, a disconnect reporting its failure,
  10 s without associating, or 20 s associated without an IP. Associated without an IP
  (AP up, DHCP or gateway down) is a failed join: after 20 s the STA is disconnected and
  the normal retry interval applies. No connect is issued while the STA is associated
  inside that window (arduino-esp32's `STA.connect()` would first tear the association
  down, blocking up to 1 s, and re-associate).
- When neither device is on WiFi they meet on `espnow_channel`.
- While the SoftAP provisioning portal is up (no WiFi credentials), the link manager
  leaves the channel to the portal: it tracks link state but never changes channel or
  probes.
- The display leaves WiFi once it is associated, MQTT has been down for 60 s and the
  controller is not linked. The MQTT-down clock starts on the first association with MQTT
  down and keeps running across short gaps in association, so the 20 s failed joins of a
  DHCP outage add up to a suspend. A gap of 2 minutes or more off WiFi restarts it, so a
  fresh association after a real WiFi outage gets a full 60 s for MQTT. While suspended
  the display keeps the STA disconnected and retries WiFi every 5 minutes, giving each
  retry a 60 s window before it may suspend again. A display with MQTT disabled or no
  MQTT host never suspends WiFi (there is no broker to wait for).
- WiFi rejoin attempts slow from every 5 s to every 60 s while ESP-NOW carries the link.
- Unicast peer required: with a broadcast or empty peer MAC the link is not trusted for
  reboot decisions and WiFi is never suspended for it.
- The controller's channel search assumes a single unicast peer in `devices` (production
  has exactly one, the display). With several, the link follows whichever peer acks a
  probe first.
- The WiFi watchdog (`include/wifi_watchdog.h`) never reboots. After 2 minutes of
  gateway unreachability it forces a reconnect through a hook that points at
  `WifiProvisioningManager::request_reconnect()`, so the join is tracked and ESP-NOW
  leaves the channel alone. It only acts while the STA has an IP, so it never re-bounces
  an STA that is associated without one.

Logic: `include/transport/espnow_channel_policy.h` (unit-tested). Radio glue:
`include/transport/espnow_link_manager.h`.

`/status` fields (both devices): `espnow_channel`, `espnow_search_count`,
`net_reboot_count`, `net_reboot_in_s`, `wifi_disconnect_reason`, `espnow_connected`.
The controller's `/status` also has `connection_path`; the display's `/status` instead has
`wifi_suspended`, and its connection path is only published on MQTT
(`state/connection_path`).
`error_espnow` reports `send_failed` only for send failures in the last 5 minutes
(`include/transport/espnow_fail_window.h`), and clears itself.

## Network Reboots

`NetworkRebootGovernor` (`include/network_reboot_governor.h`) is the only code that
reboots a device for network reasons. A device is isolated when MQTT is down and its
ESP-NOW peer is not linked (not heard for 30 s). After 2 minutes isolated it
re-initialises the network in place: ESP-NOW is deinitialised before Wi-Fi is stopped,
then Wi-Fi mode, ESP-NOW and MQTT are brought back (on Waveshare only ESP-NOW is
restarted). The in-place recovery repeats every 30 minutes while isolation lasts (2, 32,
62 ... minutes), so a device at a high reboot count does not wait a day between
recoveries. It reboots after 10 minutes isolated, then 1 hour, then 4 hours, then at most
once per 24 hours; a reboot due at the same moment as a recovery wins. The count is
stored in NVS (`net_rb_cnt`) and resets after 10 healthy (not isolated) minutes. While
the stored count is 0, isolation is not counted until the IP network has been usable once
this boot.

MQTT down while the ESP-NOW peer is linked is not isolation: the furnace is still
controlled, only Home Assistant loses the device. A wedged WiFi stack would never recover
from that by itself, so after 60 minutes of continuous MQTT-down the governor recovers in
place (same procedure), and again at 120, 180 ... minutes. It never reboots on this path.
MQTT counts as up when it is not expected: MQTT disabled, no host configured, the display
in ESP-NOW-only mode, or the display's WiFi intentionally suspended by the link policy (a
suspend/resume cycle restarts the 60-minute clock). The same network-ready gate applies.
`net_reboot_in_s` is 0 when isolation is not being counted, when the reboot is due, and
after it was issued.

## Remaining Work
- ~~Bind real display/UI events and local sensor sources into `ThermostatApp` methods.~~ Done.
- ~~Add optional ACK/diagnostic counters and explicit send result handling.~~ Done — send OK/fail counters are tracked and published as telemetry.
- Finalize encryption key handling and peer provisioning strategy.
- Note: when MQTT-primary is active, ESP-NOW commands are gated on the controller to avoid conflicting sources.
