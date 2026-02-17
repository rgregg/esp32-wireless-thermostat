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
  thermostat::ControllerApp app(tx);

  CommandWord cmd;
  cmd.mode = FurnaceMode::Heat;
  cmd.fan = FanMode::Automatic;
  cmd.setpoint_decic = 210;
  cmd.seq = 1;
  ASSERT_TRUE(app.on_command_word(espnow_cmd::encode(cmd)).accepted);

  app.on_heartbeat(1000);
  app.on_indoor_temperature_c(19.0f);
  app.tick(2000);
  ASSERT_TRUE(app.runtime().heat_demand());

  app.on_indoor_temperature_c(22.0f);
  app.tick(3000);
  ASSERT_TRUE(!app.runtime().heat_demand());
  ASSERT_TRUE(tx.count > 0);
}
#endif
