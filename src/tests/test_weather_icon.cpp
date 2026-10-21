#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "weather_icon.h"

using thermostat::WeatherIcon;
using thermostat::weather_icon_display_text;
using thermostat::weather_icon_from_condition;

TEST_CASE(weather_condition_parses_controller_display_names) {
  ASSERT_TRUE(weather_icon_from_condition("Sunny") == WeatherIcon::Sunny);
  ASSERT_TRUE(weather_icon_from_condition("Partly Cloudy") == WeatherIcon::PartlyCloudy);
  ASSERT_TRUE(weather_icon_from_condition("Night Cloudy") == WeatherIcon::NightCloudy);
  ASSERT_TRUE(weather_icon_from_condition("night cloudy") == WeatherIcon::NightCloudy);
  ASSERT_TRUE(weather_icon_from_condition("RAIN") == WeatherIcon::Rain);
}

TEST_CASE(weather_condition_parses_api_icon_names) {
  ASSERT_TRUE(weather_icon_from_condition("clear-day") == WeatherIcon::Sunny);
  ASSERT_TRUE(weather_icon_from_condition("partly-cloudy-night") == WeatherIcon::NightCloudy);
}

TEST_CASE(weather_condition_round_trips_every_display_name) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(WeatherIcon::Unknown); ++i) {
    const auto icon = static_cast<WeatherIcon>(i);
    ASSERT_TRUE(weather_icon_from_condition(weather_icon_display_text(icon)) == icon);
  }
}

TEST_CASE(weather_condition_rejects_unknown_and_prefixes) {
  ASSERT_TRUE(weather_icon_from_condition(nullptr) == WeatherIcon::Unknown);
  ASSERT_TRUE(weather_icon_from_condition("") == WeatherIcon::Unknown);
  ASSERT_TRUE(weather_icon_from_condition("Sun") == WeatherIcon::Unknown);
  ASSERT_TRUE(weather_icon_from_condition("Sunny day") == WeatherIcon::Unknown);
  ASSERT_TRUE(weather_icon_from_condition("tornado") == WeatherIcon::Unknown);
}

#endif
