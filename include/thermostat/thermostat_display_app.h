#pragma once

#include <string>

#include "thermostat/display_model.h"
#include "thermostat/thermostat_app.h"
#include "thermostat/thermostat_ui_state.h"

namespace thermostat {

class ThermostatDisplayApp {
 public:
  explicit ThermostatDisplayApp(ThermostatApp &thermostat_app);

  void set_temperature_unit(TemperatureUnit unit);
  TemperatureUnit temperature_unit() const { return model_.temperature_unit(); }
  void set_local_temperature_compensation_c(float value) {
    local_temperature_compensation_c_ = value;
  }
  float local_temperature_compensation_c() const {
    return local_temperature_compensation_c_;
  }

  void sync_from_app();

  void on_local_sensor_update(float indoor_temp_c, float indoor_humidity);
  void on_outdoor_weather_update(float outdoor_temp_c, const std::string &condition);

  void on_user_set_setpoint(float user_value, uint32_t now_ms);
  void on_user_set_setpoint_c(float setpoint_c, uint32_t now_ms);
  void on_user_set_mode(FurnaceMode mode, uint32_t now_ms);
  void on_user_set_fan_mode(FanMode mode, uint32_t now_ms);

  FurnaceMode local_mode() const { return app_.local_mode(); }
  FanMode local_fan_mode() const { return app_.local_fan_mode(); }
  float local_setpoint_c() const { return app_.local_setpoint_c(); }

  FurnaceStateCode controller_state() const { return app_.controller_state(); }
  std::string status_text(uint32_t now_ms, uint32_t connection_timeout_ms) const;
  std::string setpoint_text() const { return model_.format_setpoint_text(); }
  std::string indoor_temp_text() const { return model_.format_indoor_temperature_text(); }
  std::string indoor_humidity_text() const { return model_.format_indoor_humidity_text(); }
  std::string weather_text() const { return model_.format_weather_text(); }
  WeatherIcon weather_icon() const { return model_.weather_icon(); }
  uint32_t filter_runtime_hours() const {
    return app_.controller_filter_runtime_seconds() / 3600U;
  }

 private:
  ThermostatApp &app_;
  DisplayModel model_;
  float local_temperature_compensation_c_ = 0.0f;
};

}  // namespace thermostat
