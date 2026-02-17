#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat_app.h"
#include "transport/espnow_packets.h"
#include "test_harness.h"

namespace {
class FakeThermostatTransport : public thermostat::IThermostatTransport {
 public:
  void publish_command_word(uint32_t packed_word) override {
    last_cmd = packed_word;
    ++cmd_count;
  }
  void publish_indoor_temperature_c(float temp_c) override { last_temp = temp_c; }
  void publish_indoor_humidity(float humidity_pct) override { last_hum = humidity_pct; }

  uint32_t last_cmd = 0;
  int cmd_count = 0;
  float last_temp = 0.0f;
  float last_hum = 0.0f;
};
}  // namespace

TEST_CASE(thermostat_app_command_and_debounce) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  app.set_local_setpoint_c(22.5f, 1000);
  ASSERT_TRUE(tx.cmd_count == 1);

  thermostat::ThermostatControllerTelemetry telem;
  telem.mode_code = 2;
  telem.fan_code = 1;
  telem.setpoint_c = 18.0f;

  app.on_controller_telemetry(2000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 22.5f, 0.01f);

  app.on_controller_telemetry(7000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 18.0f, 0.01f);

  app.publish_indoor_temperature_c(21.2f);
  app.publish_indoor_humidity(44.0f);
  ASSERT_NEAR(tx.last_temp, 21.2f, 0.01f);
  ASSERT_NEAR(tx.last_hum, 44.0f, 0.01f);
}
#endif
