#pragma once

#include <stdint.h>

#include "thermostat/thermostat_app.h"
#include "thermostat/transport/espnow_thermostat_transport.h"

namespace thermostat {

class ThermostatNode {
 public:
  ThermostatNode(const ThermostatAppConfig &app_config,
                 const EspNowThermostatConfig &transport_config);

  bool begin();
  void tick(uint32_t now_ms);

  ThermostatApp &app() { return app_; }
  EspNowThermostatTransport &transport() { return transport_; }

 private:
  static void on_heartbeat_static(uint32_t now_ms, void *ctx);
  static void on_telemetry_static(const ThermostatControllerTelemetry &telemetry,
                                  void *ctx);

  void on_heartbeat(uint32_t now_ms);
  void on_telemetry(const ThermostatControllerTelemetry &telemetry);

  EspNowThermostatTransport transport_;
  ThermostatApp app_;
  EspNowThermostatConfig transport_config_{};
};

}  // namespace thermostat
