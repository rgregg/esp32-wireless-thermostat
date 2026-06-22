#if defined(THERMOSTAT_RUN_TESTS)

#include "controller/controller_task_config.h"
#include "test_harness.h"

// Regression guard (2026-06-22): the controller's MQTT client task must NEVER be pinned
// to Core 0. On Core 0 (shared with the lwIP tiT task + WiFi under LWIP_TCPIP_CORE_LOCKING)
// PubSubClient::connect()'s no-yield CONNACK busy-spin starves tiT, so the inbound CONNACK
// is never buffered within the socket timeout -> mqtt_state -4 -> stop() -> endless ~5s
// reconnect loop. That broke MQTT, ESP-NOW RX, and weather on the legacy WiFi controller
// and caused the OTA to roll back. Keep it on Core 1.
//
// controller_task_config.h also enforces this at compile time via static_assert (a Core-0
// value won't build). This test additionally checks the value in CI and pins intent, and
// fails if the constant is ever set to anything other than Core 1. NOTE: the actual task
// creation in esp32_controller_main.cpp must pass kCtrlMqttTaskCore (not a literal core),
// so changing the core requires touching this guarded constant.
TEST_CASE(controller_mqtt_task_not_on_core0) {
  ASSERT_TRUE(thermostat::kCtrlMqttTaskCore != 0);
  ASSERT_EQ(thermostat::kCtrlMqttTaskCore, 1);
}

#endif  // THERMOSTAT_RUN_TESTS
