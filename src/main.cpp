#include <stdint.h>

#include "controller/controller_app.h"
#include "espnow_cmd_word.h"

namespace {

class DemoTransport final : public thermostat::IControllerTransport {
 public:
  void publish_telemetry(const thermostat::ControllerTelemetry &telemetry) override {
    last = telemetry;
    ++publish_count;
  }

  thermostat::ControllerTelemetry last{};
  uint32_t publish_count = 0;
};

}  // namespace

int run_smoke_checks() {
  DemoTransport transport;

  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 5000;
  cfg.fan_circulate_period_min = 5;
  cfg.fan_circulate_duration_min = 2;

  thermostat::ControllerApp app(transport, cfg);

  CommandWord first;
  first.mode = FurnaceMode::Cool;
  first.fan = FanMode::Automatic;
  first.setpoint_decic = 230;
  first.seq = 10;

  const auto first_result = app.on_command_word(espnow_cmd::encode(first));
  if (!first_result.accepted) {
    return 1;
  }

  const auto duplicate_result = app.on_command_word(espnow_cmd::encode(first));
  if (!duplicate_result.stale_or_duplicate) {
    return 2;
  }

  app.on_indoor_temperature_c(25.0f);
  app.on_heartbeat(1000);
  app.tick(2000);
  if (!app.runtime().cool_demand() || app.runtime().heat_demand() ||
      app.runtime().fan_demand()) {
    return 3;
  }

  app.tick(8000);
  if (!app.runtime().failsafe_active() || app.runtime().cool_demand() ||
      app.runtime().heat_demand() || app.runtime().fan_demand()) {
    return 4;
  }

  app.on_heartbeat(9000);
  app.tick(9001);

  CommandWord second;
  second.mode = FurnaceMode::Off;
  second.fan = FanMode::AlwaysOn;
  second.setpoint_decic = 210;
  second.seq = 11;
  const auto second_result = app.on_command_word(espnow_cmd::encode(second));
  if (!second_result.accepted) {
    return 5;
  }

  app.tick(9100);
  if (!app.runtime().fan_demand()) {
    return 6;
  }

  if (transport.publish_count == 0) {
    return 7;
  }

  return 0;
}

int main() { return run_smoke_checks(); }
