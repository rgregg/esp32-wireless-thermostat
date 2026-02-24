#include "thermostat/display_model.h"

#include <cmath>

namespace thermostat {

void DisplayModel::set_temperature_unit(TemperatureUnit unit) { unit_ = unit; }

void DisplayModel::set_local_setpoint_c(float setpoint_c) { local_setpoint_c_ = setpoint_c; }

void DisplayModel::set_local_indoor_temperature_c(float value) { indoor_temp_c_ = value; }

void DisplayModel::set_local_indoor_humidity(float value) { indoor_humidity_ = value; }

void DisplayModel::set_outdoor_temperature_c(float value) { outdoor_temp_c_ = value; }

void DisplayModel::set_weather_icon(WeatherIcon icon) {
  weather_icon_ = icon;
}

std::string DisplayModel::format_setpoint_text() const {
  if (std::isnan(local_setpoint_c_)) {
    return "N/A";
  }
  char buf[16];
  const float v = to_user_temperature(local_setpoint_c_);
  if (unit_ == TemperatureUnit::Fahrenheit) {
    snprintf(buf, sizeof(buf), "%.0f", std::round(v));
  } else {
    snprintf(buf, sizeof(buf), "%.1f", v);
  }
  return std::string(buf);
}

std::string DisplayModel::format_indoor_temperature_text() const {
  if (std::isnan(indoor_temp_c_)) {
    return "N/A";
  }
  char buf[16];
  const float v = to_user_temperature(indoor_temp_c_);
  if (unit_ == TemperatureUnit::Fahrenheit) {
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", std::round(v));
  } else {
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", v);
  }
  return std::string(buf);
}

std::string DisplayModel::format_indoor_humidity_text() const {
  if (std::isnan(indoor_humidity_)) {
    return "";
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f%% humidity", indoor_humidity_);
  return std::string(buf);
}

std::string DisplayModel::format_weather_text() const {
  char buf[64];
  const float v = to_user_temperature(outdoor_temp_c_);
  snprintf(buf, sizeof(buf), "%s %.0f\xC2\xB0",
           weather_icon_display_text(weather_icon_), std::round(v));
  return std::string(buf);
}

float DisplayModel::to_user_temperature(float celsius) const {
  if (unit_ == TemperatureUnit::Fahrenheit) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
  }
  return celsius;
}

float DisplayModel::to_celsius_from_user(float value) const {
  if (unit_ == TemperatureUnit::Fahrenheit) {
    return (value - 32.0f) * 5.0f / 9.0f;
  }
  return value;
}

}  // namespace thermostat
