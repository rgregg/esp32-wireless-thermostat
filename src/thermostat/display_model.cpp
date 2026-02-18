#include "thermostat/display_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace thermostat {

namespace {

std::string normalize_weather(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

void DisplayModel::set_temperature_unit(TemperatureUnit unit) { unit_ = unit; }

void DisplayModel::set_local_setpoint_c(float setpoint_c) { local_setpoint_c_ = setpoint_c; }

void DisplayModel::set_local_indoor_temperature_c(float value) { indoor_temp_c_ = value; }

void DisplayModel::set_local_indoor_humidity(float value) { indoor_humidity_ = value; }

void DisplayModel::set_outdoor_temperature_c(float value) { outdoor_temp_c_ = value; }

void DisplayModel::set_weather_condition(const std::string &value) {
  weather_condition_ = value;
}

std::string DisplayModel::format_setpoint_text() const {
  char buf[16];
  const float v = to_user_temperature(local_setpoint_c_);
  if (unit_ == TemperatureUnit::Fahrenheit) {
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", std::round(v));
  } else {
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", v);
  }
  return std::string(buf);
}

std::string DisplayModel::format_indoor_temperature_text() const {
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
  char buf[24];
  snprintf(buf, sizeof(buf), "%.0f%% humidity", indoor_humidity_);
  return std::string(buf);
}

std::string DisplayModel::format_weather_text() const {
  char buf[64];
  const float v = to_user_temperature(outdoor_temp_c_);
  snprintf(buf, sizeof(buf), "%s %.0f\xC2\xB0", weather_condition_.c_str(), std::round(v));
  return std::string(buf);
}

WeatherIcon DisplayModel::weather_icon() const {
  const std::string cond = normalize_weather(weather_condition_);

  if (cond == "sunny" || cond == "clear") return WeatherIcon::Sunny;
  if (cond == "partly cloudy" || cond == "partlycloudy" || cond == "partly_cloudy")
    return WeatherIcon::PartlyCloudy;
  if (cond == "cloudy" || cond == "overcast") return WeatherIcon::Cloudy;
  if (cond == "rain" || cond == "showers") return WeatherIcon::Rain;
  if (cond == "heavy rain" || cond == "rain_heavy") return WeatherIcon::RainHeavy;
  if (cond == "light rain" || cond == "rain_light") return WeatherIcon::RainLight;
  if (cond == "lightning" || cond == "storm" || cond == "thunderstorm")
    return WeatherIcon::Lightning;
  if (cond == "snow") return WeatherIcon::Snow;
  if (cond == "light snow" || cond == "snow_light") return WeatherIcon::SnowLight;
  if (cond == "sleet") return WeatherIcon::Sleet;
  if (cond == "hail") return WeatherIcon::Hail;
  if (cond == "windy" || cond == "wind") return WeatherIcon::Windy;
  if (cond == "fog") return WeatherIcon::Fog;
  if (cond == "haze") return WeatherIcon::Haze;
  if (cond == "dust") return WeatherIcon::Dust;
  if (cond == "dry") return WeatherIcon::Dry;
  if (cond == "night") return WeatherIcon::Night;
  if (cond == "night cloudy" || cond == "night_cloudy") return WeatherIcon::NightCloudy;

  return WeatherIcon::Unknown;
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
