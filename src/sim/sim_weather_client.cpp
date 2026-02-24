#include "sim_weather_client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "controller/pirateweather.h"

namespace sim {

namespace {

// libcurl write callback: appends data to std::string
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  const size_t total = size * nmemb;
  out->append(ptr, total);
  return total;
}

// Simple HTTP GET via libcurl. Returns true + body on HTTP 200.
bool http_get(const std::string &url, std::string *body_out) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) return false;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_out);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  }
  curl_easy_cleanup(curl);

  return res == CURLE_OK && http_code == 200;
}

}  // namespace

WeatherResult fetch_weather(const std::string &api_key, const std::string &zip) {
  WeatherResult result;

  if (api_key.empty() || zip.empty()) return result;

  const std::string norm_zip = pirateweather::normalize_zip(zip.c_str());
  if (norm_zip.empty()) return result;

  // Step 1: Resolve ZIP -> lat/lon via zippopotam.us
  std::string geo_body;
  const std::string geo_url = pirateweather::geocode_url(norm_zip.c_str());
  if (!http_get(geo_url, &geo_body)) {
    fprintf(stderr, "[Weather] Failed to fetch coordinates for ZIP %s\n", norm_zip.c_str());
    return result;
  }

  float lat = 0.0f, lon = 0.0f;
  if (!pirateweather::parse_geocode_response(geo_body.c_str(), &lat, &lon)) {
    fprintf(stderr, "[Weather] Failed to parse coordinates from zippopotam response\n");
    return result;
  }

  // Step 2: Fetch current weather from PirateWeather
  const std::string weather_url =
      pirateweather::forecast_url(api_key.c_str(), lat, lon);

  std::string weather_body;
  if (!http_get(weather_url, &weather_body)) {
    fprintf(stderr, "[Weather] Failed to fetch PirateWeather data\n");
    return result;
  }

  float temp_c = 0.0f;
  thermostat::WeatherIcon icon = thermostat::WeatherIcon::Unknown;
  if (!pirateweather::parse_forecast_response(weather_body.c_str(), &temp_c, &icon)) {
    fprintf(stderr, "[Weather] Failed to parse PirateWeather response\n");
    return result;
  }

  result.ok = true;
  result.temp_c = temp_c;
  result.icon = icon;
  return result;
}

}  // namespace sim
