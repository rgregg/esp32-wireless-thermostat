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
only when neither device is on WiFi.

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
  earlier frame never acknowledges a later probe.
- All ESP-NOW sends happen on the main loop task, which the probe attribution relies on.
  Command-triggered telemetry on the controller and the display's controller ACK are
  deferred to the loop tick rather than sent from the receive callback.
- The link manager pauses while a tracked WiFi join is running (`join_in_progress`, at
  most 10 s per attempt): no channel change, probe or enforcement, and any outstanding
  probe is dropped. When a join ends unassociated, the radio is put back on the wanted
  channel (checked once a second). Tracking: `include/wifi_join_tracker.h`.
- When neither device is on WiFi they meet on `espnow_channel`.
- The display leaves WiFi only after it has been associated for 60 s with MQTT down and
  the controller not heard. It then keeps the STA disconnected and retries WiFi every
  5 minutes, giving each retry a 60 s window before it may suspend again.
- WiFi rejoin attempts slow from every 5 s to every 60 s while ESP-NOW carries the link.
- Unicast peer required: with a broadcast or empty peer MAC the link is not trusted for
  reboot decisions and WiFi is never suspended for it.
- The WiFi watchdog (`include/wifi_watchdog.h`) never reboots. After 2 minutes of
  gateway unreachability it forces a reconnect through a hook that points at
  `WifiProvisioningManager::request_reconnect()`, so the join is tracked and ESP-NOW
  leaves the channel alone.

Logic: `include/transport/espnow_channel_policy.h` (unit-tested). Radio glue:
`include/transport/espnow_link_manager.h`.

`/status` fields: `espnow_channel`, `connection_path`, `espnow_search_count`,
`net_reboot_count`, `net_reboot_in_s`, `wifi_disconnect_reason`, `espnow_connected`.
`error_espnow` reports `send_failed` only for send failures in the last 5 minutes
(`include/transport/espnow_fail_window.h`), and clears itself.

## Network Reboots

`NetworkRebootGovernor` (`include/network_reboot_governor.h`) is the only code that
reboots a device for network reasons. A device is isolated when MQTT is down and its
ESP-NOW peer is not linked (not heard for 30 s). After 2 minutes isolated it
re-initialises the network in place: ESP-NOW is deinitialised before Wi-Fi is stopped,
then Wi-Fi mode, ESP-NOW and MQTT are brought back (on Waveshare only ESP-NOW is
restarted). It reboots after 10 minutes isolated, then 1 hour, then 4 hours, then at most
once per 24 hours. The count is stored in NVS (`net_rb_cnt`) and resets after 10 healthy
minutes. While the stored count is 0, isolation is not counted until the IP network has
been usable once this boot.

## Remaining Work
- ~~Bind real display/UI events and local sensor sources into `ThermostatApp` methods.~~ Done.
- ~~Add optional ACK/diagnostic counters and explicit send result handling.~~ Done — send OK/fail counters are tracked and published as telemetry.
- Finalize encryption key handling and peer provisioning strategy.
- Note: when MQTT-primary is active, ESP-NOW commands are gated on the controller to avoid conflicting sources.
