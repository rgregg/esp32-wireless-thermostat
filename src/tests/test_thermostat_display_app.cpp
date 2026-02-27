#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat/thermostat_app.h"
#include "thermostat/thermostat_display_app.h"
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
  void publish_indoor_temperature_c(float temp_c) override {
    last_temp = temp_c;
    ++temp_count;
  }
  void publish_indoor_humidity(float humidity_pct) override {
    last_humidity = humidity_pct;
    ++humidity_count;
  }

  uint32_t last_cmd = 0;
  int cmd_count = 0;
  uint16_t last_ack_seq = 0;
  int ack_count = 0;
  float last_temp = 0.0f;
  float last_humidity = 0.0f;
  int temp_count = 0;
  int humidity_count = 0;
};
}  // namespace

TEST_CASE(thermostat_display_app_user_and_sensor_flow) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);
  thermostat::ThermostatDisplayApp display(app);

  display.set_temperature_unit(thermostat::TemperatureUnit::Fahrenheit);
  display.on_user_set_setpoint(72.0f, 1000);
  ASSERT_TRUE(tx.cmd_count == 1);
  ASSERT_NEAR(app.local_setpoint_c(), 22.2f, 0.2f);

  display.set_local_temperature_compensation_c(1.5f);
  display.on_local_sensor_update(21.5f, 45.0f);
  ASSERT_TRUE(tx.temp_count == 1);
  ASSERT_TRUE(tx.humidity_count == 1);
  ASSERT_NEAR(tx.last_temp, 23.0f, 0.01f);

  display.on_outdoor_weather_update(0.0f, thermostat::WeatherIcon::Sunny);
  ASSERT_TRUE(display.weather_icon() == thermostat::WeatherIcon::Sunny);
}

TEST_CASE(thermostat_display_app_status_text) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);
  thermostat::ThermostatDisplayApp display(app);

  thermostat::ThermostatControllerTelemetry telem;
  telem.seq = 1;
  telem.state = FurnaceStateCode::HeatOn;
  telem.lockout = false;
  app.on_controller_heartbeat(1000);
  app.on_controller_telemetry(1000, telem);

  ASSERT_TRUE(display.status_text(1500, 30000) == "Heat on");
}

TEST_CASE(thermostat_display_app_controller_state_update_pass_through) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);
  thermostat::ThermostatDisplayApp display(app);

  display.on_controller_state_update(
      1000, FurnaceStateCode::CoolOn, false,
      FurnaceMode::Cool, FanMode::Circulate, 24.0f, 3600);

  // Verify state propagated through to app
  ASSERT_TRUE(app.has_controller_telemetry());
  ASSERT_EQ(static_cast<int>(app.controller_state()),
            static_cast<int>(FurnaceStateCode::CoolOn));
  ASSERT_EQ(static_cast<int>(display.local_mode()),
            static_cast<int>(FurnaceMode::Cool));
  ASSERT_EQ(static_cast<int>(display.local_fan_mode()),
            static_cast<int>(FanMode::Circulate));

  // Heartbeat updated - controller connected
  ASSERT_TRUE(display.status_text(1500, 30000) == "Cool on");
}
#endif
