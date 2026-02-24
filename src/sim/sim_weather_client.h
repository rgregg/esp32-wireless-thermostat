#pragma once

#include <string>

#include "weather_icon.h"

namespace sim {

struct WeatherResult {
  bool ok = false;
  float temp_c = 0.0f;
  thermostat::WeatherIcon icon = thermostat::WeatherIcon::Unknown;
};

// Fetch current weather from PirateWeather API using libcurl.
// 1. Resolves ZIP code to lat/lon via zippopotam.us
// 2. Fetches current conditions from PirateWeather
// Returns result with ok=true on success.
WeatherResult fetch_weather(const std::string &api_key, const std::string &zip);

}  // namespace sim
