#include "controller/pirateweather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pirateweather {

namespace {

// Extract a JSON string value by key (simple substring search, no JSON library).
// Searches for "key":"value" starting from start_pos.
bool json_extract_string(const char *json, const char *key,
                         char *out, size_t out_len, size_t start_pos = 0) {
  if (json == nullptr || key == nullptr || out == nullptr || out_len == 0) {
    return false;
  }

  // Build "key" search needle
  char needle[128];
  int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(needle)) return false;

  const char *haystack = json + start_pos;
  const char *pos = strstr(haystack, needle);
  if (pos == nullptr) return false;

  // Find the colon after the key
  pos += strlen(needle);
  pos = strchr(pos, ':');
  if (pos == nullptr) return false;
  ++pos;

  // Skip whitespace
  while (*pos == ' ' || *pos == '\t') ++pos;
  if (*pos == '\0') return false;

  if (*pos == '"') {
    // Quoted string value
    ++pos;
    size_t i = 0;
    while (*pos != '\0' && *pos != '"' && i < out_len - 1) {
      out[i++] = *pos++;
    }
    out[i] = '\0';
    return *pos == '"';
  }

  // Unquoted value (number) — read until delimiter
  size_t i = 0;
  while (*pos != '\0' && *pos != ',' && *pos != '}' &&
         *pos != ']' && *pos != ' ' && i < out_len - 1) {
    out[i++] = *pos++;
  }
  out[i] = '\0';
  return i > 0;
}

// Extract a float value from JSON by key.
bool json_extract_float(const char *json, const char *key,
                        float *out, size_t start_pos = 0) {
  char buf[64];
  if (!json_extract_string(json, key, buf, sizeof(buf), start_pos)) {
    return false;
  }
  char *end = nullptr;
  float f = strtof(buf, &end);
  if (end == buf) return false;
  *out = f;
  return true;
}

}  // namespace

std::string normalize_zip(const char *raw) {
  if (raw == nullptr) return std::string();
  std::string out;
  out.reserve(5);
  for (const char *p = raw; *p != '\0'; ++p) {
    if (*p == '-') break;
    if (*p >= '0' && *p <= '9') {
      if (out.size() < 5) out += *p;
    }
  }
  return out.size() == 5 ? out : std::string();
}

std::string geocode_url(const char *zip) {
  return std::string("https://api.zippopotam.us/us/") + zip;
}

bool parse_geocode_response(const char *json, float *lat, float *lon) {
  if (json == nullptr || lat == nullptr || lon == nullptr) return false;
  return json_extract_float(json, "latitude", lat) &&
         json_extract_float(json, "longitude", lon);
}

std::string forecast_url(const char *api_key, float lat, float lon) {
  char coord[40];
  snprintf(coord, sizeof(coord), "%.5f,%.5f",
           static_cast<double>(lat), static_cast<double>(lon));
  return std::string("https://api.pirateweather.net/forecast/") + api_key +
         "/" + coord + "?units=si&exclude=minutely,hourly,daily,alerts";
}

bool parse_forecast_response(const char *json, float *temp_c,
                             char *condition, size_t condition_len) {
  if (json == nullptr || temp_c == nullptr ||
      condition == nullptr || condition_len == 0) {
    return false;
  }

  // Find "currently" block
  const char *currently = strstr(json, "\"currently\"");
  if (currently == nullptr) return false;

  size_t offset = static_cast<size_t>(currently - json);

  if (!json_extract_float(json, "temperature", temp_c, offset)) {
    return false;
  }

  char icon[64] = {0};
  json_extract_string(json, "icon", icon, sizeof(icon), offset);

  const char *mapped = map_icon(icon);
  snprintf(condition, condition_len, "%s", mapped);
  return true;
}

const char *map_icon(const char *icon) {
  if (icon == nullptr || icon[0] == '\0') return "Unknown";
  if (strcmp(icon, "clear-day") == 0) return "Sunny";
  if (strcmp(icon, "clear-night") == 0) return "Night";
  if (strcmp(icon, "partly-cloudy-day") == 0) return "Partly Cloudy";
  if (strcmp(icon, "partly-cloudy-night") == 0) return "Night Cloudy";
  if (strcmp(icon, "cloudy") == 0) return "Cloudy";
  if (strcmp(icon, "fog") == 0) return "Fog";
  if (strcmp(icon, "rain") == 0) return "Rain";
  if (strcmp(icon, "snow") == 0) return "Snow";
  if (strcmp(icon, "sleet") == 0) return "Sleet";
  if (strcmp(icon, "wind") == 0) return "Windy";
  if (strcmp(icon, "hail") == 0) return "Hail";
  if (strcmp(icon, "thunderstorm") == 0) return "Lightning";
  return icon;
}

}  // namespace pirateweather
