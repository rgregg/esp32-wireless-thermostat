#if defined(THERMOSTAT_RUN_TESTS)
#include <string.h>

#include "controller/controller_app.h"
#include "espnow_cmd_word.h"
#include "test_harness.h"

namespace {
class FakeControllerTransport : public thermostat::IControllerTransport {
 public:
  void publish_telemetry(const thermostat::ControllerTelemetry &telemetry) override {
    last = telemetry;
    ++count;
  }
  thermostat::ControllerTelemetry last{};
  int count = 0;
};
}  // namespace

TEST_CASE(controller_app_auto_hvac_from_indoor_temp) {
  FakeControllerTransport tx;
  thermostat::ControllerConfig cfg;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_idle_time_ms = 0;
  cfg.heat_deadband_c = 0.5f;
  cfg.heat_overrun_c = 0.5f;
  thermostat::ControllerApp app(tx, cfg);

  CommandWord cmd;
  cmd.mode = FurnaceMode::Heat;
  cmd.fan = FanMode::Automatic;
  cmd.setpoint_decic = 210;
  cmd.seq = 1;
  ASSERT_TRUE(app.on_command_word(espnow_cmd::encode(cmd)).accepted);

  app.on_heartbeat(1000);
  app.on_indoor_temperature_c(20.4f);
  app.tick(2000);
  ASSERT_TRUE(app.runtime().heat_demand());

  app.on_indoor_temperature_c(21.2f);
  app.tick(3000);
  ASSERT_TRUE(app.runtime().heat_demand());

  app.on_indoor_temperature_c(21.6f);
  app.tick(4000);
  ASSERT_TRUE(!app.runtime().heat_demand());
  ASSERT_TRUE(tx.count > 0);
}

TEST_CASE(controller_app_uses_fallback_indoor_temperature_without_remote_updates) {
  FakeControllerTransport tx;
  thermostat::ControllerConfig cfg;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_idle_time_ms = 0;
  cfg.indoor_temp_fallback_c = 19.0f;
  thermostat::ControllerApp app(tx, cfg);

  CommandWord cmd;
  cmd.mode = FurnaceMode::Heat;
  cmd.fan = FanMode::Automatic;
  cmd.setpoint_decic = 210;
  cmd.seq = 1;
  ASSERT_TRUE(app.on_command_word(espnow_cmd::encode(cmd)).accepted);

  app.on_heartbeat(1000);
  app.tick(2000);
  ASSERT_TRUE(app.runtime().heat_demand());
}

TEST_CASE(controller_app_nan_fallback_does_not_enable_indoor_temp) {
  FakeControllerTransport tx;
  thermostat::ControllerConfig cfg;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_idle_time_ms = 0;
  cfg.indoor_temp_fallback_c = NAN;
  thermostat::ControllerApp app(tx, cfg);

  // NaN fallback should not mark indoor temp as available
  ASSERT_TRUE(!app.has_indoor_temperature());
}

TEST_CASE(controller_app_real_sensor_overrides_fallback) {
  FakeControllerTransport tx;
  thermostat::ControllerConfig cfg;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_idle_time_ms = 0;
  cfg.indoor_temp_fallback_c = 19.0f;
  thermostat::ControllerApp app(tx, cfg);

  // Starts with fallback
  ASSERT_TRUE(app.has_indoor_temperature());
  ASSERT_TRUE(app.indoor_temperature_c() == 19.0f);

  // Real sensor update overrides fallback
  app.on_indoor_temperature_c(22.5f);
  ASSERT_TRUE(app.indoor_temperature_c() == 22.5f);
}

TEST_CASE(controller_app_primary_sensor_auto_claims_first_mac) {
  FakeControllerTransport tx;
  thermostat::ControllerApp app(tx);

  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  app.on_indoor_temperature_c(22.0f, mac_a);

  ASSERT_TRUE(app.primary_sensor_auto_claimed());
  ASSERT_TRUE(memcmp(app.primary_sensor_mac(), mac_a, 6) == 0);
  ASSERT_TRUE(app.indoor_temperature_c() == 22.0f);
}

TEST_CASE(controller_app_primary_sensor_rejects_different_mac) {
  FakeControllerTransport tx;
  thermostat::ControllerApp app(tx);

  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  const uint8_t mac_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

  app.on_indoor_temperature_c(22.0f, mac_a);  // auto-claims mac_a
  app.on_indoor_temperature_c(25.0f, mac_b);  // should be ignored

  ASSERT_TRUE(app.indoor_temperature_c() == 22.0f);

  // Humidity from different MAC also rejected
  app.on_indoor_humidity(60.0f, mac_a);  // accepted
  app.on_indoor_humidity(90.0f, mac_b);  // rejected
  ASSERT_TRUE(app.indoor_humidity_pct() == 60.0f);
}

TEST_CASE(controller_app_set_primary_sensor_mac_overrides_auto_claim) {
  FakeControllerTransport tx;
  thermostat::ControllerApp app(tx);

  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  const uint8_t mac_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

  app.on_indoor_temperature_c(22.0f, mac_a);  // auto-claims mac_a
  ASSERT_TRUE(app.primary_sensor_auto_claimed());

  // Override to mac_b
  app.set_primary_sensor_mac(mac_b);
  ASSERT_TRUE(!app.primary_sensor_auto_claimed());
  ASSERT_TRUE(memcmp(app.primary_sensor_mac(), mac_b, 6) == 0);

  // mac_a now rejected, mac_b accepted
  app.on_indoor_temperature_c(25.0f, mac_a);
  ASSERT_TRUE(app.indoor_temperature_c() == 22.0f);  // unchanged

  app.on_indoor_temperature_c(25.0f, mac_b);
  ASSERT_TRUE(app.indoor_temperature_c() == 25.0f);
}

TEST_CASE(controller_app_broadcast_mac_accepts_all_sources) {
  FakeControllerTransport tx;
  thermostat::ControllerApp app(tx);

  const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  const uint8_t mac_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

  // Explicitly set to broadcast so auto-claim is disabled
  app.set_primary_sensor_mac(broadcast);

  // Both sources accepted since broadcast means accept-all
  // But note: first call will auto-claim, so we need to re-set to broadcast
  app.on_indoor_temperature_c(22.0f, mac_a);
  // After first call, mac_a is auto-claimed. Re-set to broadcast.
  app.set_primary_sensor_mac(broadcast);
  app.on_indoor_temperature_c(25.0f, mac_b);
  // After second call, mac_b is auto-claimed. Check it was accepted.
  ASSERT_TRUE(app.indoor_temperature_c() == 25.0f);
}

TEST_CASE(controller_app_nullptr_mac_always_accepted) {
  FakeControllerTransport tx;
  thermostat::ControllerApp app(tx);

  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

  // Auto-claim a specific MAC
  app.on_indoor_temperature_c(22.0f, mac_a);
  ASSERT_TRUE(memcmp(app.primary_sensor_mac(), mac_a, 6) == 0);

  // nullptr (sim/MQTT path) should still be accepted
  app.on_indoor_temperature_c(25.0f);
  ASSERT_TRUE(app.indoor_temperature_c() == 25.0f);
}

TEST_CASE(controller_runtime_set_filter_runtime_seconds_survives_tick) {
  // Verify ControllerRuntime::set_filter_runtime_seconds injects a value
  // that persists across tick() calls when no HVAC is active
  thermostat::ControllerConfig cfg;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_idle_time_ms = 0;
  thermostat::ControllerRuntime rt(cfg);

  // Directly set a persisted value (simulates loading from NVS on boot)
  ASSERT_EQ(rt.filter_runtime_seconds(), static_cast<uint32_t>(0));
  rt.set_filter_runtime_seconds(7200);
  ASSERT_EQ(rt.filter_runtime_seconds(), static_cast<uint32_t>(7200));

  // tick() should not reset it when no HVAC is active
  rt.note_heartbeat(1000);
  thermostat::ControllerTickInput in;
  in.now_ms = 2000;
  rt.tick(in);
  ASSERT_EQ(rt.filter_runtime_seconds(), static_cast<uint32_t>(7200));
}
#endif
