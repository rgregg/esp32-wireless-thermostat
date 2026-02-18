#include "thermostat_display_app.h"

namespace thermostat {

ThermostatDisplayApp::ThermostatDisplayApp(ThermostatApp &thermostat_app)
    : app_(thermostat_app) {
  model_.set_local_setpoint_c(app_.local_setpoint_c());
}

void ThermostatDisplayApp::set_temperature_unit(TemperatureUnit unit) {
  model_.set_temperature_unit(unit);
}

void ThermostatDisplayApp::sync_from_app() {
  model_.set_local_setpoint_c(app_.local_setpoint_c());
}

void ThermostatDisplayApp::on_local_sensor_update(float indoor_temp_c,
                                                  float indoor_humidity) {
  const float compensated_temp_c = indoor_temp_c + local_temperature_compensation_c_;
  model_.set_local_indoor_temperature_c(compensated_temp_c);
  model_.set_local_indoor_humidity(indoor_humidity);
  app_.publish_indoor_temperature_c(compensated_temp_c);
  app_.publish_indoor_humidity(indoor_humidity);
}

void ThermostatDisplayApp::on_outdoor_weather_update(float outdoor_temp_c,
                                                     const std::string &condition) {
  model_.set_outdoor_temperature_c(outdoor_temp_c);
  model_.set_weather_condition(condition);
}

void ThermostatDisplayApp::on_user_set_setpoint(float user_value, uint32_t now_ms) {
  const float celsius = model_.to_celsius_from_user(user_value);
  app_.set_local_setpoint_c(celsius, now_ms);
  model_.set_local_setpoint_c(app_.local_setpoint_c());
}

void ThermostatDisplayApp::on_user_set_setpoint_c(float setpoint_c, uint32_t now_ms) {
  app_.set_local_setpoint_c(setpoint_c, now_ms);
  model_.set_local_setpoint_c(app_.local_setpoint_c());
}

void ThermostatDisplayApp::on_user_set_mode(FurnaceMode mode, uint32_t now_ms) {
  app_.set_local_mode(mode, now_ms);
}

void ThermostatDisplayApp::on_user_set_fan_mode(FanMode mode, uint32_t now_ms) {
  app_.set_local_fan_mode(mode, now_ms);
}

std::string ThermostatDisplayApp::status_text(uint32_t now_ms,
                                              uint32_t connection_timeout_ms) const {
  const bool connected = app_.controller_connected(now_ms, connection_timeout_ms);
  const bool lockout = app_.has_controller_telemetry() ? app_.controller_lockout() : false;
  const auto state =
      app_.has_controller_telemetry() ? app_.controller_state() : FurnaceStateCode::Error;
  return furnace_state_text(state, connected, lockout, false);
}

}  // namespace thermostat
