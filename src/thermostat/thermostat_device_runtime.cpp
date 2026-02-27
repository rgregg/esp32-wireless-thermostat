#include "thermostat/thermostat_device_runtime.h"

namespace thermostat {

ThermostatDeviceRuntime::ThermostatDeviceRuntime(
    const ThermostatDeviceRuntimeConfig &config)
    : config_(config), node_(config.app, config.transport), display_(node_.app()) {
  node_.set_weather_callback(&ThermostatDeviceRuntime::on_weather_from_controller, this);
}

bool ThermostatDeviceRuntime::begin() { return node_.begin(); }

void ThermostatDeviceRuntime::on_weather_from_controller(float outdoor_temp_c,
                                                          WeatherIcon icon,
                                                          void *ctx) {
  if (ctx == nullptr) return;
  auto *self = static_cast<ThermostatDeviceRuntime *>(ctx);
  self->display_.on_outdoor_weather_update(outdoor_temp_c, icon);
  self->last_controller_weather_ms_ = 0;  // mark as received; firmware sets actual timestamp
  self->has_controller_weather_ = true;
}

void ThermostatDeviceRuntime::tick(uint32_t now_ms) {
  node_.tick(now_ms);
  display_.sync_from_app();
}

void ThermostatDeviceRuntime::on_local_sensor_update(float indoor_temp_c,
                                                     float indoor_humidity) {
  display_.on_local_sensor_update(indoor_temp_c, indoor_humidity);
}

void ThermostatDeviceRuntime::on_outdoor_weather_update(float outdoor_temp_c,
                                                        WeatherIcon icon) {
  display_.on_outdoor_weather_update(outdoor_temp_c, icon);
}

void ThermostatDeviceRuntime::on_controller_state_update(
    uint32_t now_ms, FurnaceStateCode state, bool lockout, FurnaceMode mode,
    FanMode fan, float setpoint_c, uint32_t filter_runtime_seconds) {
  display_.on_controller_state_update(now_ms, state, lockout, mode, fan,
                                      setpoint_c, filter_runtime_seconds);
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

void ThermostatDeviceRuntime::reset_local_command_sequence() {
  node_.app().reset_local_command_sequence();
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

FurnaceStateCode ThermostatDeviceRuntime::controller_state() const {
  return display_.controller_state();
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

float ThermostatDeviceRuntime::local_indoor_temperature_c() const {
  return display_.local_indoor_temperature_c();
}

float ThermostatDeviceRuntime::local_indoor_humidity() const {
  return display_.local_indoor_humidity();
}

std::string ThermostatDeviceRuntime::weather_text() const {
  return display_.weather_text();
}

WeatherIcon ThermostatDeviceRuntime::weather_icon() const { return display_.weather_icon(); }

uint32_t ThermostatDeviceRuntime::filter_runtime_hours() const {
  return display_.filter_runtime_hours();
}

}  // namespace thermostat
