#pragma once

#include <stdint.h>

#include <string>

#include "thermostat_types.h"
#include "thermostat/thermostat_ui_state.h"
#include "weather_icon.h"

namespace thermostat {

enum class TemperatureUnit : uint8_t {
  Fahrenheit = 0,
  Celsius = 1,
};

class DisplayModel {
 public:
  void set_temperature_unit(TemperatureUnit unit);
  TemperatureUnit temperature_unit() const { return unit_; }

  void set_local_setpoint_c(float setpoint_c);
  float local_setpoint_c() const { return local_setpoint_c_; }

  void set_local_indoor_temperature_c(float value);
  void set_local_indoor_humidity(float value);
  void set_outdoor_temperature_c(float value);
  void set_weather_icon(WeatherIcon icon);

  std::string format_setpoint_text() const;
  std::string format_indoor_temperature_text() const;
  std::string format_indoor_humidity_text() const;
  std::string format_weather_text() const;
  WeatherIcon weather_icon() const { return weather_icon_; }

  float to_user_temperature(float celsius) const;
  float to_celsius_from_user(float value) const;

 private:
  TemperatureUnit unit_ = TemperatureUnit::Fahrenheit;
  float local_setpoint_c_ = 20.0f;
  float indoor_temp_c_ = 20.0f;
  float indoor_humidity_ = 50.0f;
  float outdoor_temp_c_ = 10.0f;
  WeatherIcon weather_icon_ = WeatherIcon::Unknown;
};

}  // namespace thermostat
