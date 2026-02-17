#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat_device_runtime.h"
#include "test_harness.h"

TEST_CASE(thermostat_device_runtime_status_defaults) {
  thermostat::ThermostatDeviceRuntimeConfig cfg;
  thermostat::ThermostatDeviceRuntime runtime(cfg);

  ASSERT_TRUE(runtime.begin());
  runtime.on_local_sensor_update(22.0f, 40.0f);
  runtime.on_outdoor_weather_update(5.0f, "Sunny");
  runtime.tick(1000);

  ASSERT_TRUE(!runtime.setpoint_text().empty());
  ASSERT_TRUE(!runtime.indoor_temp_text().empty());
}

TEST_CASE(thermostat_device_runtime_user_inputs_flow_to_display_app) {
  thermostat::ThermostatDeviceRuntimeConfig cfg;
  thermostat::ThermostatDeviceRuntime runtime(cfg);

  ASSERT_TRUE(runtime.begin());

  runtime.set_temperature_unit(thermostat::TemperatureUnit::Celsius);
  runtime.on_user_set_mode(FurnaceMode::Cool, 1000);
  runtime.on_user_set_fan_mode(FanMode::Circulate, 1000);
  runtime.on_user_set_setpoint(22.5f, 1000);
  runtime.on_local_sensor_update(23.0f, 41.0f);
  runtime.on_outdoor_weather_update(7.0f, "Cloudy");
  runtime.tick(1100);

  ASSERT_EQ(runtime.temperature_unit(), thermostat::TemperatureUnit::Celsius);
  ASSERT_EQ(runtime.local_mode(), FurnaceMode::Cool);
  ASSERT_EQ(runtime.local_fan_mode(), FanMode::Circulate);
  ASSERT_NEAR(runtime.local_setpoint_c(), 22.5f, 0.05f);
  ASSERT_TRUE(runtime.status_text(1100).size() > 0);
  ASSERT_TRUE(runtime.weather_text().find("Cloudy") != std::string::npos);
}
#endif
