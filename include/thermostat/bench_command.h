#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "thermostat_types.h"

namespace thermostat {

// A bench-test command for the display, entered over USB serial (`cmd mode=heat sp=22.5`)
// or HTTP (`POST /cmd`). It goes through the same runtime calls as the touchscreen, so
// HIL tests can drive the display's command path while it has no MQTT.
struct BenchCommand {
  bool has_mode = false;
  FurnaceMode mode = FurnaceMode::Off;
  bool has_fan = false;
  FanMode fan = FanMode::Automatic;
  bool has_setpoint = false;
  float setpoint_c = 0.0f;
  bool sync = false;

  bool empty() const { return !has_mode && !has_fan && !has_setpoint && !sync; }
};

// Parses `key=value` tokens separated by spaces, '&' or ','. Keys: mode (heat|cool|off),
// fan (auto|on|circulate), sp (degrees C, 0-40), sync (1). Returns false, with a short
// reason in *error, on an unknown key, a bad value or no fields at all.
inline bool parse_bench_command(const char *line, BenchCommand *out, const char **error) {
  static const char *kNoError = "";
  *error = kNoError;
  BenchCommand cmd;
  if (line == nullptr) line = "";
  char buf[128];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  if (strlen(line) >= sizeof(buf)) {
    *error = "too long";
    return false;
  }
  char *save = nullptr;
  for (char *tok = strtok_r(buf, " &,\t\r\n", &save); tok != nullptr;
       tok = strtok_r(nullptr, " &,\t\r\n", &save)) {
    char *eq = strchr(tok, '=');
    if (eq == nullptr || eq == tok || eq[1] == '\0') {
      *error = "expected key=value";
      return false;
    }
    *eq = '\0';
    const char *key = tok;
    const char *val = eq + 1;
    if (strcmp(key, "mode") == 0) {
      if (strcmp(val, "heat") == 0) cmd.mode = FurnaceMode::Heat;
      else if (strcmp(val, "cool") == 0) cmd.mode = FurnaceMode::Cool;
      else if (strcmp(val, "off") == 0) cmd.mode = FurnaceMode::Off;
      else { *error = "mode: heat|cool|off"; return false; }
      cmd.has_mode = true;
    } else if (strcmp(key, "fan") == 0) {
      if (strcmp(val, "auto") == 0) cmd.fan = FanMode::Automatic;
      else if (strcmp(val, "on") == 0) cmd.fan = FanMode::AlwaysOn;
      else if (strcmp(val, "circulate") == 0) cmd.fan = FanMode::Circulate;
      else { *error = "fan: auto|on|circulate"; return false; }
      cmd.has_fan = true;
    } else if (strcmp(key, "sp") == 0) {
      char *end = nullptr;
      const float c = strtof(val, &end);
      if (end == val || *end != '\0' || !isfinite(c) || c < 0.0f || c > 40.0f) {
        *error = "sp: 0-40 (C)";
        return false;
      }
      cmd.setpoint_c = c;
      cmd.has_setpoint = true;
    } else if (strcmp(key, "sync") == 0) {
      if (strcmp(val, "1") != 0) { *error = "sync: 1"; return false; }
      cmd.sync = true;
    } else {
      *error = "unknown key (mode, fan, sp, sync)";
      return false;
    }
  }
  if (cmd.empty()) {
    *error = "no fields";
    return false;
  }
  *out = cmd;
  return true;
}

}  // namespace thermostat
