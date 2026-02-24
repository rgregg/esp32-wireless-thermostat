#pragma once

#include <stddef.h>

#include <string>

#include "weather_icon.h"

namespace pirateweather {

// Normalize ZIP to 5-digit string. Returns empty string on invalid input.
std::string normalize_zip(const char *raw);

// Build zippopotam geocoding URL for a normalized ZIP.
std::string geocode_url(const char *zip);

// Parse lat/lon from zippopotam JSON response.
bool parse_geocode_response(const char *json, float *lat, float *lon);

// Build PirateWeather forecast URL.
std::string forecast_url(const char *api_key, float lat, float lon);

// Parse current weather from PirateWeather JSON response.
// Writes temperature and WeatherIcon. Returns true on success.
bool parse_forecast_response(const char *json, float *temp_c,
                             thermostat::WeatherIcon *icon_out);

// Map PirateWeather icon string to WeatherIcon enum.
inline thermostat::WeatherIcon map_icon(const char *icon) {
  return thermostat::weather_icon_from_api(icon);
}

}  // namespace pirateweather
