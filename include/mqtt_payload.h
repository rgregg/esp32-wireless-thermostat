#pragma once

#include "thermostat_types.h"
#include <string.h>

namespace mqtt_payload {

// --- Mode/Fan enum <-> MQTT string ---

inline const char *mode_to_str(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Heat: return "heat";
    case FurnaceMode::Cool: return "cool";
    default: return "off";
  }
}

inline FurnaceMode str_to_mode(const char *s) {
  if (strcmp(s, "heat") == 0) return FurnaceMode::Heat;
  if (strcmp(s, "cool") == 0) return FurnaceMode::Cool;
  return FurnaceMode::Off;
}

inline const char *fan_to_str(FanMode mode) {
  switch (mode) {
    case FanMode::AlwaysOn: return "on";
    case FanMode::Circulate: return "circulate";
    default: return "auto";
  }
}

inline FanMode str_to_fan(const char *s) {
  if (strcmp(s, "on") == 0 || strcmp(s, "always on") == 0) return FanMode::AlwaysOn;
  if (strcmp(s, "circulate") == 0) return FanMode::Circulate;
  return FanMode::Automatic;
}

// --- Bool payload parsing ---

inline bool parse_bool(const char *s) {
  if (s == nullptr) return false;
  return strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "on") == 0;
}

}  // namespace mqtt_payload
