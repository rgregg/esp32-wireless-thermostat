#pragma once

#include <stdint.h>

#include <string>

#include "thermostat/thermostat_display_app.h"
#include "thermostat/thermostat_node.h"

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
  void on_user_set_setpoint_c(float setpoint_c, uint32_t now_ms);
  void on_user_set_mode(FurnaceMode mode, uint32_t now_ms);
  void on_user_set_fan_mode(FanMode mode, uint32_t now_ms);
  void request_sync(uint32_t now_ms);
  void request_filter_reset(uint32_t now_ms);
  void reset_local_command_sequence();
  void set_temperature_unit(TemperatureUnit unit);
  TemperatureUnit temperature_unit() const;
  void set_local_temperature_compensation_c(float value);
  float local_temperature_compensation_c() const;

  FurnaceMode local_mode() const;
  FanMode local_fan_mode() const;
  float local_setpoint_c() const;
  bool has_last_packed_command() const;
  uint32_t last_packed_command() const;
  uint16_t last_command_seq() const;
  uint32_t last_controller_heartbeat_ms() const;
  uint32_t espnow_send_ok_count() const;
  uint32_t espnow_send_fail_count() const;

  FurnaceStateCode controller_state() const;
  std::string status_text(uint32_t now_ms) const;
  std::string setpoint_text() const;
  std::string indoor_temp_text() const;
  std::string indoor_humidity_text() const;
  std::string weather_text() const;
  WeatherIcon weather_icon() const;
  uint32_t filter_runtime_hours() const;

  bool has_controller_weather() const { return has_controller_weather_; }
  void set_last_controller_weather_ms(uint32_t ms) { last_controller_weather_ms_ = ms; }
  uint32_t last_controller_weather_ms() const { return last_controller_weather_ms_; }

 private:
  static void on_weather_from_controller(float outdoor_temp_c, const char *condition,
                                          void *ctx);

  ThermostatDeviceRuntimeConfig config_;
  ThermostatNode node_;
  ThermostatDisplayApp display_;
  bool has_controller_weather_ = false;
  uint32_t last_controller_weather_ms_ = 0;
};

}  // namespace thermostat
