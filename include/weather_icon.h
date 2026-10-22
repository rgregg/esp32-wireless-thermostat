#pragma once

#include <stdint.h>
#include <string.h>

namespace thermostat {

enum class WeatherIcon : uint8_t {
  Sunny,
  PartlyCloudy,
  Cloudy,
  Rain,
  RainHeavy,
  RainLight,
  Lightning,
  Snow,
  SnowLight,
  Sleet,
  Hail,
  Windy,
  Fog,
  Haze,
  Dust,
  Dry,
  Night,
  NightCloudy,
  Unknown,
};

// Map PirateWeather API icon string to WeatherIcon enum value.
inline WeatherIcon weather_icon_from_api(const char *icon) {
  if (icon == nullptr || icon[0] == '\0') return WeatherIcon::Unknown;
  if (strcmp(icon, "clear-day") == 0) return WeatherIcon::Sunny;
  if (strcmp(icon, "clear-night") == 0) return WeatherIcon::Night;
  if (strcmp(icon, "partly-cloudy-day") == 0) return WeatherIcon::PartlyCloudy;
  if (strcmp(icon, "partly-cloudy-night") == 0) return WeatherIcon::NightCloudy;
  if (strcmp(icon, "cloudy") == 0) return WeatherIcon::Cloudy;
  if (strcmp(icon, "fog") == 0) return WeatherIcon::Fog;
  if (strcmp(icon, "rain") == 0) return WeatherIcon::Rain;
  if (strcmp(icon, "snow") == 0) return WeatherIcon::Snow;
  if (strcmp(icon, "sleet") == 0) return WeatherIcon::Sleet;
  if (strcmp(icon, "wind") == 0) return WeatherIcon::Windy;
  if (strcmp(icon, "hail") == 0) return WeatherIcon::Hail;
  if (strcmp(icon, "thunderstorm") == 0) return WeatherIcon::Lightning;
  return WeatherIcon::Unknown;
}

// Map WeatherIcon enum to English display string.
inline const char *weather_icon_display_text(WeatherIcon icon) {
  switch (icon) {
    case WeatherIcon::Sunny: return "Sunny";
    case WeatherIcon::PartlyCloudy: return "Partly Cloudy";
    case WeatherIcon::Cloudy: return "Cloudy";
    case WeatherIcon::Rain: return "Rain";
    case WeatherIcon::RainHeavy: return "Heavy Rain";
    case WeatherIcon::RainLight: return "Light Rain";
    case WeatherIcon::Lightning: return "Lightning";
    case WeatherIcon::Snow: return "Snow";
    case WeatherIcon::SnowLight: return "Light Snow";
    case WeatherIcon::Sleet: return "Sleet";
    case WeatherIcon::Hail: return "Hail";
    case WeatherIcon::Windy: return "Windy";
    case WeatherIcon::Fog: return "Fog";
    case WeatherIcon::Haze: return "Haze";
    case WeatherIcon::Dust: return "Dust";
    case WeatherIcon::Dry: return "Dry";
    case WeatherIcon::Night: return "Night";
    case WeatherIcon::NightCloudy: return "Night Cloudy";
    case WeatherIcon::Unknown:
    default: return "Unknown";
  }
}

// Parse a condition received over MQTT: a PirateWeather API icon name ("clear-day") or a
// display name as the controller publishes it ("Sunny", "Partly Cloudy"), ignoring case.
inline WeatherIcon weather_icon_from_condition(const char *condition) {
  const WeatherIcon api = weather_icon_from_api(condition);
  if (api != WeatherIcon::Unknown || condition == nullptr) return api;
  for (uint8_t i = 0; i < static_cast<uint8_t>(WeatherIcon::Unknown); ++i) {
    const auto candidate = static_cast<WeatherIcon>(i);
    const char *name = weather_icon_display_text(candidate);
    size_t j = 0;
    for (; name[j] != '\0' && condition[j] != '\0'; ++j) {
      char a = name[j], b = condition[j];
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) break;
    }
    if (name[j] == '\0' && condition[j] == '\0') return candidate;
  }
  return WeatherIcon::Unknown;
}

}  // namespace thermostat
