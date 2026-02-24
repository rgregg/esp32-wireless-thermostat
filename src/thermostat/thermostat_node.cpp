#include "thermostat/thermostat_node.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace thermostat {

ThermostatNode::ThermostatNode(const ThermostatAppConfig &app_config,
                               const EspNowThermostatConfig &transport_config)
    : transport_(), app_(transport_, app_config), transport_config_(transport_config) {}

bool ThermostatNode::begin() {
  transport_.set_callbacks(&ThermostatNode::on_heartbeat_static,
                           &ThermostatNode::on_telemetry_static,
                           this);
  transport_.set_weather_callback(&ThermostatNode::on_weather_static);
  const bool ok = transport_.begin(transport_config_);
  if (ok) {
#if defined(ARDUINO)
    app_.request_sync(millis());
#else
    app_.request_sync(0);
#endif
  }
  return ok;
}

void ThermostatNode::tick(uint32_t now_ms) {
  transport_.loop(now_ms);
}

void ThermostatNode::on_heartbeat_static(uint32_t now_ms, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ThermostatNode *>(ctx)->on_heartbeat(now_ms);
}

void ThermostatNode::on_telemetry_static(const ThermostatControllerTelemetry &telemetry,
                                         void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ThermostatNode *>(ctx)->on_telemetry(telemetry);
}

void ThermostatNode::on_heartbeat(uint32_t now_ms) {
  app_.on_controller_heartbeat(now_ms);
}

void ThermostatNode::on_telemetry(const ThermostatControllerTelemetry &telemetry) {
#if defined(ARDUINO)
  app_.on_controller_telemetry(millis(), telemetry);
#else
  app_.on_controller_telemetry(0, telemetry);
#endif
}

void ThermostatNode::on_weather_static(float outdoor_temp_c, WeatherIcon icon,
                                        void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ThermostatNode *>(ctx)->on_weather(outdoor_temp_c, icon);
}

void ThermostatNode::on_weather(float outdoor_temp_c, WeatherIcon icon) {
  if (weather_cb_ != nullptr) {
    weather_cb_(outdoor_temp_c, icon, weather_cb_ctx_);
  }
}

}  // namespace thermostat
