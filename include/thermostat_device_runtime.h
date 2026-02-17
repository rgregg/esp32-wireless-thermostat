#pragma once

#include <stdint.h>

#include <string>

#include "thermostat_display_app.h"
#include "thermostat_node.h"

namespace thermostat {

struct ThermostatDeviceRuntimeConfig {
  ThermostatAppConfig app{};
  EspNowThermostatConfig transport{};
  uint32_t controller_connection_timeout_ms = 30000;
};

class ThermostatDeviceRuntime {
 public:
  explicit ThermostatDeviceRuntime(const ThermostatDeviceRuntimeConfig &config);

  bool begin();
  void tick(uint32_t now_ms);

  void on_local_sensor_update(float indoor_temp_c, float indoor_humidity);
  void on_outdoor_weather_update(float outdoor_temp_c, const std::string &condition);

  void on_user_set_setpoint(float user_value, uint32_t now_ms);
  void on_user_set_mode(FurnaceMode mode, uint32_t now_ms);
  void on_user_set_fan_mode(FanMode mode, uint32_t now_ms);

  std::string status_text(uint32_t now_ms) const;
  std::string setpoint_text() const;
  std::string indoor_temp_text() const;

 private:
  ThermostatDeviceRuntimeConfig config_;
  ThermostatNode node_;
  ThermostatDisplayApp display_;
};

}  // namespace thermostat
