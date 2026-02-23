#pragma once

#include <string>

namespace sim {

struct WeatherResult {
  bool ok = false;
  float temp_c = 0.0f;
  std::string condition;
};

// Fetch current weather from PirateWeather API using libcurl.
// 1. Resolves ZIP code to lat/lon via zippopotam.us
// 2. Fetches current conditions from PirateWeather
// Returns result with ok=true on success.
WeatherResult fetch_weather(const std::string &api_key, const std::string &zip);

}  // namespace sim
