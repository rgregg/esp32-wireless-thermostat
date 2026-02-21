#include "thermostat/thermostat_device_runtime.h"

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

void ThermostatDeviceRuntime::on_user_set_setpoint_c(float setpoint_c, uint32_t now_ms) {
  display_.on_user_set_setpoint_c(setpoint_c, now_ms);
}

void ThermostatDeviceRuntime::on_user_set_mode(FurnaceMode mode, uint32_t now_ms) {
  display_.on_user_set_mode(mode, now_ms);
}

void ThermostatDeviceRuntime::on_user_set_fan_mode(FanMode mode, uint32_t now_ms) {
  display_.on_user_set_fan_mode(mode, now_ms);
}

void ThermostatDeviceRuntime::request_sync(uint32_t now_ms) {
  node_.app().request_sync(now_ms);
}

void ThermostatDeviceRuntime::request_filter_reset(uint32_t now_ms) {
  node_.app().request_filter_reset(now_ms);
}

void ThermostatDeviceRuntime::set_temperature_unit(TemperatureUnit unit) {
  display_.set_temperature_unit(unit);
}

TemperatureUnit ThermostatDeviceRuntime::temperature_unit() const {
  return display_.temperature_unit();
}

void ThermostatDeviceRuntime::set_local_temperature_compensation_c(float value) {
  display_.set_local_temperature_compensation_c(value);
}

float ThermostatDeviceRuntime::local_temperature_compensation_c() const {
  return display_.local_temperature_compensation_c();
}

FurnaceMode ThermostatDeviceRuntime::local_mode() const { return display_.local_mode(); }

FanMode ThermostatDeviceRuntime::local_fan_mode() const { return display_.local_fan_mode(); }

float ThermostatDeviceRuntime::local_setpoint_c() const {
  return display_.local_setpoint_c();
}

bool ThermostatDeviceRuntime::has_last_packed_command() const {
  return node_.app().has_last_packed_command();
}

uint32_t ThermostatDeviceRuntime::last_packed_command() const {
  return node_.app().last_packed_command();
}

uint16_t ThermostatDeviceRuntime::last_command_seq() const {
  return node_.app().last_command_seq();
}

uint32_t ThermostatDeviceRuntime::last_controller_heartbeat_ms() const {
  return node_.app().last_controller_heartbeat_ms();
}

uint32_t ThermostatDeviceRuntime::espnow_send_ok_count() const {
  return node_.transport().send_ok_count();
}

uint32_t ThermostatDeviceRuntime::espnow_send_fail_count() const {
  return node_.transport().send_fail_count();
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

std::string ThermostatDeviceRuntime::indoor_humidity_text() const {
  return display_.indoor_humidity_text();
}

std::string ThermostatDeviceRuntime::weather_text() const {
  return display_.weather_text();
}

WeatherIcon ThermostatDeviceRuntime::weather_icon() const { return display_.weather_icon(); }

}  // namespace thermostat
