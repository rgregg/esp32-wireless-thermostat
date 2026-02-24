#pragma once

#include <stdint.h>

#include "thermostat/thermostat_app.h"
#include "thermostat/transport/espnow_thermostat_transport.h"
#include "weather_icon.h"

namespace thermostat {

class ThermostatNode {
 public:
  using WeatherCallback = void (*)(float outdoor_temp_c, WeatherIcon icon,
                                   void *ctx);

  ThermostatNode(const ThermostatAppConfig &app_config,
                 const EspNowThermostatConfig &transport_config);

  bool begin();
  void tick(uint32_t now_ms);

  void set_weather_callback(WeatherCallback cb, void *ctx) {
    weather_cb_ = cb;
    weather_cb_ctx_ = ctx;
  }

  ThermostatApp &app() { return app_; }
  const ThermostatApp &app() const { return app_; }
  EspNowThermostatTransport &transport() { return transport_; }
  const EspNowThermostatTransport &transport() const { return transport_; }

 private:
  static void on_heartbeat_static(uint32_t now_ms, void *ctx);
  static void on_telemetry_static(const ThermostatControllerTelemetry &telemetry,
                                  void *ctx);
  static void on_weather_static(float outdoor_temp_c, WeatherIcon icon,
                                void *ctx);

  void on_heartbeat(uint32_t now_ms);
  void on_telemetry(const ThermostatControllerTelemetry &telemetry);
  void on_weather(float outdoor_temp_c, WeatherIcon icon);

  WeatherCallback weather_cb_ = nullptr;
  void *weather_cb_ctx_ = nullptr;

  EspNowThermostatTransport transport_;
  ThermostatApp app_;
  EspNowThermostatConfig transport_config_{};
};

}  // namespace thermostat
