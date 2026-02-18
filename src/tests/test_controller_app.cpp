#if defined(THERMOSTAT_RUN_TESTS)
#include "controller_app.h"
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
#endif
