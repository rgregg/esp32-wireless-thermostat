#if defined(THERMOSTAT_RUN_TESTS)
#include <cstdio>
#include <cstring>

#include "test_harness.h"

// PubSubClient buffer size used by both controller and thermostat.
// Payloads exceeding this are silently dropped by PubSubClient.
static constexpr size_t kMqttBufferSize = 1024;

// Use a generous topic base and device ID to simulate realistic worst-case.
// IDs include a MAC suffix (e.g. "_abcdef") as devices now append one by default.
static const char *kBase = "thermostat/my-long-furnace-controller-name";
static const char *kCtrlDevId = "my_long_wireless_thermostat_system_id_abcdef_controller";
static const char *kDispDevId = "my_long_wireless_thermostat_system_id_abcdef_display";

// The climate discovery payload is the largest single MQTT message published.
// It must fit within the PubSubClient buffer to actually reach the broker.
TEST_CASE(mqtt_discovery_climate_payload_fits_buffer) {
  char payload[2048];
  // Uses HA's ~ (tilde) abbreviation to keep payload compact
  int len = snprintf(
      payload, sizeof(payload),
      "{\"~\":\"%s\",\"name\":\"Furnace Thermostat\",\"uniq_id\":\"%s_climate\","
      "\"mode_cmd_t\":\"~/cmd/mode\",\"mode_stat_t\":\"~/state/mode\","
      "\"temp_cmd_t\":\"~/cmd/target_temp_c\",\"temp_stat_t\":\"~/state/target_temp_c\","
      "\"curr_temp_t\":\"~/state/current_temp_c\","
      "\"fan_mode_cmd_t\":\"~/cmd/fan_mode\",\"fan_mode_stat_t\":\"~/state/fan_mode\","
      "\"curr_hum_t\":\"~/state/current_humidity\","
      "\"modes\":[\"off\",\"heat\",\"cool\"],"
      "\"fan_modes\":[\"auto\",\"on\",\"circulate\"],"
      "\"avty_t\":\"~/state/availability\","
      "\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\","
      "\"min_temp\":5,\"max_temp\":35,\"temp_step\":0.5,\"temp_unit\":\"C\","
      "\"dev\":{\"ids\":[\"%s\"],"
      "\"name\":\"Furnace Controller\",\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
      kBase, kCtrlDevId, kCtrlDevId);

  ASSERT_TRUE(len > 0);
  ASSERT_TRUE(static_cast<size_t>(len) < kMqttBufferSize);
}

TEST_CASE(mqtt_discovery_sensor_payload_fits_buffer) {
  char payload[2048];

  // Indoor temperature sensor (thermostat display)
  int len = snprintf(
      payload, sizeof(payload),
      "{\"name\":\"Indoor Temperature\",\"uniq_id\":\"%s_indoor_temperature\","
      "\"stat_t\":\"%s/state/current_temp_c\",\"unit_of_meas\":\"\xC2\xB0""C\","
      "\"dev_cla\":\"temperature\",\"stat_cla\":\"measurement\","
      "\"dev\":{\"ids\":[\"%s\"],\"name\":\"Thermostat Display\","
      "\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
      kDispDevId, kBase, kDispDevId);
  ASSERT_TRUE(len > 0);
  ASSERT_TRUE(static_cast<size_t>(len) < kMqttBufferSize);

  // Indoor humidity sensor (thermostat display)
  len = snprintf(
      payload, sizeof(payload),
      "{\"name\":\"Indoor Humidity\",\"uniq_id\":\"%s_indoor_humidity\","
      "\"stat_t\":\"%s/state/current_humidity\",\"unit_of_meas\":\"%%\","
      "\"dev_cla\":\"humidity\",\"stat_cla\":\"measurement\","
      "\"dev\":{\"ids\":[\"%s\"]}}",
      kDispDevId, kBase, kDispDevId);
  ASSERT_TRUE(len > 0);
  ASSERT_TRUE(static_cast<size_t>(len) < kMqttBufferSize);
}

TEST_CASE(mqtt_discovery_switch_payload_fits_buffer) {
  char payload[2048];
  int len = snprintf(
      payload, sizeof(payload),
      "{\"name\":\"HVAC Lockout\",\"uniq_id\":\"%s_lockout\",\"cmd_t\":\"%s/cmd/lockout\","
      "\"stat_t\":\"%s/state/lockout\",\"pl_on\":\"1\",\"pl_off\":\"0\","
      "\"dev\":{\"ids\":[\"%s\"]}}",
      kCtrlDevId, kBase, kBase, kCtrlDevId);
  ASSERT_TRUE(len > 0);
  ASSERT_TRUE(static_cast<size_t>(len) < kMqttBufferSize);
}

TEST_CASE(mqtt_discovery_number_payload_fits_buffer) {
  char payload[2048];
  // Display timeout is the largest number entity
  int len = snprintf(
      payload, sizeof(payload),
      "{\"name\":\"Display Timeout\",\"uniq_id\":\"%s_display_timeout\","
      "\"cmd_t\":\"%s/cmd/display_timeout_s\",\"stat_t\":\"%s/state/display_timeout_s\","
      "\"min\":30,\"max\":600,\"step\":5,\"mode\":\"box\",\"unit_of_meas\":\"s\","
      "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"Thermostat Display\","
      "\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
      kDispDevId, kBase, kBase, kDispDevId);
  ASSERT_TRUE(len > 0);
  ASSERT_TRUE(static_cast<size_t>(len) < kMqttBufferSize);
}

#endif
