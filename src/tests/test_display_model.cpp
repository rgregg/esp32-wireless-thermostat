#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat/display_model.h"
#include "test_harness.h"
#include "thermostat/thermostat_ui_state.h"
#include <math.h>

TEST_CASE(display_model_units_and_weather) {
  thermostat::DisplayModel m;
  m.set_temperature_unit(thermostat::TemperatureUnit::Fahrenheit);
  m.set_local_setpoint_c(20.0f);
  m.set_outdoor_temperature_c(0.0f);
  m.set_weather_icon(thermostat::WeatherIcon::PartlyCloudy);

  ASSERT_TRUE(m.format_setpoint_text() == "68");
  ASSERT_TRUE(m.weather_icon() == thermostat::WeatherIcon::PartlyCloudy);

  m.set_temperature_unit(thermostat::TemperatureUnit::Celsius);
  ASSERT_TRUE(m.format_setpoint_text() == "20.0");
}

TEST_CASE(display_model_nan_indoor_temperature_uses_na_fallback) {
  thermostat::DisplayModel m;
  m.set_local_indoor_temperature_c(NAN);
  ASSERT_TRUE(m.format_indoor_temperature_text() == "--");
}

TEST_CASE(display_model_indoor_temperature_includes_degree_symbol) {
  thermostat::DisplayModel m;
  m.set_local_indoor_temperature_c(20.0f);
  m.set_temperature_unit(thermostat::TemperatureUnit::Fahrenheit);
  ASSERT_TRUE(m.format_indoor_temperature_text() == "68\xC2\xB0");

  m.set_temperature_unit(thermostat::TemperatureUnit::Celsius);
  ASSERT_TRUE(m.format_indoor_temperature_text() == "20.0\xC2\xB0");
}

TEST_CASE(ui_state_text_mapping) {
  ASSERT_TRUE(thermostat::furnace_state_text(FurnaceStateCode::Idle, true,
                                             false, false) == "Idle");
  ASSERT_TRUE(thermostat::furnace_state_text(FurnaceStateCode::Idle, false,
                                             false, false) == "Not Connected");
  ASSERT_TRUE(thermostat::furnace_mode_text(FurnaceMode::Cool) == "Cool");
  ASSERT_TRUE(thermostat::fan_mode_text(FanMode::Circulate) == "Circulate");
}
#endif
