#pragma once

// FreeRTOS core assignment for the controller's MQTT client task.
//
// The MQTT task MUST NOT run on Core 0. PubSubClient::connect() waits for the CONNACK
// with a no-yield busy-spin on _client->available() (unlike readByte(), which yields).
// lwIP's TCP/IP task (tiT) and the WiFi task are pinned to Core 0 under
// LWIP_TCPIP_CORE_LOCKING. Running the spin on Core 0 starves tiT of the core lock, so
// the inbound CONNACK is never buffered within the socket timeout -> mqtt_state -4 ->
// stop() -> endless ~5s reconnect loop. Observed 2026-06-22 on the legacy WiFi controller
// (it also starved ESP-NOW RX and weather/HTTPS). On Core 1 the spin runs in parallel
// with tiT/WiFi. This holds on EVERY board: tiT is always Core-0-pinned, so Core 0 is a
// latent hazard regardless of WiFi vs Ethernet.
//
// The static_assert below makes a Core-0 regression a COMPILE error; the
// `controller_mqtt_task_not_on_core0` native test guards it in CI as well. The task
// creation in esp32_controller_main.cpp MUST pass kCtrlMqttTaskCore (not a literal).
//
// Platform-agnostic (plain int, no Arduino/FreeRTOS types) so it is usable from the
// native test build as well as firmware.

namespace thermostat {

constexpr int kCtrlMqttTaskCore = 1;

static_assert(kCtrlMqttTaskCore != 0,
              "MQTT task must not be pinned to Core 0: PubSubClient's no-yield CONNACK "
              "spin starves the Core-0 lwIP tiT task, causing a CONNACK-timeout reconnect "
              "loop. Keep it on Core 1.");

}  // namespace thermostat
