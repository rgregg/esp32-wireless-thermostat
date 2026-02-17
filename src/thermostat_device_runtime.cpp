#include "thermostat_device_runtime.h"

namespace thermostat {

ThermostatDeviceRuntime::ThermostatDeviceRuntime(
    const ThermostatDeviceRuntimeConfig &config)
    : config_(config), node_(config.app, config.transport), display_(node_.app()) {}

bool ThermostatDeviceRuntime::begin() { return node_.begin(); }

void ThermostatDeviceRuntime::tick(uint32_t now_ms) {
  node_.tick(now_ms);
  display_.sync_from_app();
}

void ThermostatDeviceRuntime::on_local_sensor_update(float indoor_temp_c,
                                                     float indoor_humidity) {
  display_.on_local_sensor_update(indoor_temp_c, indoor_humidity);
}

void ThermostatDeviceRuntime::on_outdoor_weather_update(float outdoor_temp_c,
                                                        const std::string &condition) {
  display_.on_outdoor_weather_update(outdoor_temp_c, condition);
}

void ThermostatDeviceRuntime::on_user_set_setpoint(float user_value, uint32_t now_ms) {
  display_.on_user_set_setpoint(user_value, now_ms);
}

void ThermostatDeviceRuntime::on_user_set_mode(FurnaceMode mode, uint32_t now_ms) {
  display_.on_user_set_mode(mode, now_ms);
}

void ThermostatDeviceRuntime::on_user_set_fan_mode(FanMode mode, uint32_t now_ms) {
  display_.on_user_set_fan_mode(mode, now_ms);
}

std::string ThermostatDeviceRuntime::status_text(uint32_t now_ms) const {
  return display_.status_text(now_ms, config_.controller_connection_timeout_ms);
}

std::string ThermostatDeviceRuntime::setpoint_text() const {
  return display_.setpoint_text();
}

std::string ThermostatDeviceRuntime::indoor_temp_text() const {
  return display_.indoor_temp_text();
}

}  // namespace thermostat
