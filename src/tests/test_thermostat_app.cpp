#if defined(THERMOSTAT_RUN_TESTS)
#include "command_builder.h"
#include "thermostat/thermostat_app.h"
#include "transport/espnow_packets.h"
#include "test_harness.h"

namespace {
class FakeThermostatTransport : public thermostat::IThermostatTransport {
 public:
  void publish_command_word(uint32_t packed_word) override {
    last_cmd = packed_word;
    ++cmd_count;
  }
  void publish_controller_ack(uint16_t seq) override {
    last_ack_seq = seq;
    ++ack_count;
  }
  void publish_indoor_temperature_c(float temp_c) override { last_temp = temp_c; }
  void publish_indoor_humidity(float humidity_pct) override { last_hum = humidity_pct; }

  uint32_t last_cmd = 0;
  int cmd_count = 0;
  uint16_t last_ack_seq = 0;
  int ack_count = 0;
  float last_temp = 0.0f;
  float last_hum = 0.0f;
};
}  // namespace

TEST_CASE(thermostat_app_command_and_debounce) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  app.set_local_setpoint_c(22.5f, 1000);
  ASSERT_TRUE(tx.cmd_count == 1);
  ASSERT_TRUE(app.has_last_packed_command());
  ASSERT_EQ(app.last_packed_command(), tx.last_cmd);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(1));
  ASSERT_EQ(tx.last_cmd, thermostat::build_packed_command(FurnaceMode::Off, FanMode::Automatic,
                                                          22.5f, 1, false, false));

  thermostat::ThermostatControllerTelemetry telem;
  telem.seq = 1;
  telem.mode_code = 2;
  telem.fan_code = 1;
  telem.setpoint_c = 18.0f;

  app.on_controller_telemetry(2000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 22.5f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 1);
  ASSERT_EQ(tx.ack_count, 1);

  telem.seq = 2;
  app.on_controller_telemetry(7000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 18.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 2);
  ASSERT_EQ(tx.ack_count, 2);

  app.request_sync(7100);
  ASSERT_TRUE(app.last_packed_command() == tx.last_cmd);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(2));
  ASSERT_EQ(tx.last_cmd, thermostat::build_packed_command(FurnaceMode::Cool, FanMode::AlwaysOn,
                                                          18.0f, 2, true, false));

  app.publish_indoor_temperature_c(21.2f);
  app.publish_indoor_humidity(44.0f);
  ASSERT_NEAR(tx.last_temp, 21.2f, 0.01f);
  ASSERT_NEAR(tx.last_hum, 44.0f, 0.01f);
}

TEST_CASE(thermostat_app_ignores_duplicate_and_stale_controller_seq) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  thermostat::ThermostatControllerTelemetry telem;
  telem.seq = 10;
  telem.mode_code = 1;
  telem.setpoint_c = 20.0f;
  app.on_controller_telemetry(1000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);

  telem.seq = 10;  // duplicate
  telem.setpoint_c = 25.0f;
  app.on_controller_telemetry(2000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);

  telem.seq = 9;  // stale
  telem.setpoint_c = 15.0f;
  app.on_controller_telemetry(3000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);
}
#endif
