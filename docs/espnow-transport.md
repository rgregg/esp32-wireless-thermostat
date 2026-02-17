# ESP-NOW Transport

## Scope
This documents the current ESP-NOW transport adapters present in the PlatformIO port.

## Packet Types
Defined in `/Users/ryan/github/rgregg/esp32-wireless-thermostat/include/transport/espnow_packets.h`.

- `Heartbeat` (`PacketType::Heartbeat`)
- `CommandWord` (`PacketType::CommandWord`)
- `ControllerTelemetry` (`PacketType::ControllerTelemetry`)
- `IndoorTemperature` (`PacketType::IndoorTemperature`)
- `IndoorHumidity` (`PacketType::IndoorHumidity`)

Protocol version: `1`.

## Implemented Adapters

Controller-side adapter:
- `/Users/ryan/github/rgregg/esp32-wireless-thermostat/include/transport/espnow_controller_transport.h`
- `/Users/ryan/github/rgregg/esp32-wireless-thermostat/src/transport/espnow_controller_transport.cpp`

Thermostat-side adapter:
- `/Users/ryan/github/rgregg/esp32-wireless-thermostat/include/transport/espnow_thermostat_transport.h`
- `/Users/ryan/github/rgregg/esp32-wireless-thermostat/src/transport/espnow_thermostat_transport.cpp`

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

## Remaining Work
- Bind real display/UI events and local sensor sources into `ThermostatApp` methods.
- Add optional ACK/diagnostic counters and explicit send result handling.
- Finalize encryption key handling and peer provisioning strategy.
