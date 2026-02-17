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
#endif
