#pragma once

#include <string>

#include "display_model.h"
#include "thermostat_app.h"
#include "thermostat_ui_state.h"

namespace thermostat {

class ThermostatDisplayApp {
 public:
  explicit ThermostatDisplayApp(ThermostatApp &thermostat_app);

  void set_temperature_unit(TemperatureUnit unit);
  TemperatureUnit temperature_unit() const { return model_.temperature_unit(); }

  void sync_from_app();

  void on_local_sensor_update(float indoor_temp_c, float indoor_humidity);
  void on_outdoor_weather_update(float outdoor_temp_c, const std::string &condition);

  void on_user_set_setpoint(float user_value, uint32_t now_ms);
  void on_user_set_mode(FurnaceMode mode, uint32_t now_ms);
  void on_user_set_fan_mode(FanMode mode, uint32_t now_ms);

  std::string status_text(uint32_t now_ms, uint32_t connection_timeout_ms) const;
  std::string setpoint_text() const { return model_.format_setpoint_text(); }
  std::string indoor_temp_text() const { return model_.format_indoor_temperature_text(); }
  std::string indoor_humidity_text() const { return model_.format_indoor_humidity_text(); }
  std::string weather_text() const { return model_.format_weather_text(); }
  WeatherIcon weather_icon() const { return model_.weather_icon(); }

 private:
  ThermostatApp &app_;
  DisplayModel model_;
};

}  // namespace thermostat
