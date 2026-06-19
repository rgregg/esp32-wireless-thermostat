#pragma once

#include "controller/relay_backend.h"
#include "thermostat_types.h"

namespace thermostat {

struct GpioRelayBackendConfig {
  int heat_pin = 32;
  int cool_pin = 33;
  int fan_pin = 25;
  int spare_pin = 26;
  bool inverted = false;
};

// Pure, testable: the digital level to drive for a given relay demand + polarity.
inline bool gpio_relay_level(bool demand_on, bool inverted) {
  return inverted ? !demand_on : demand_on;
}

class GpioRelayBackend : public RelayBackend {
 public:
  explicit GpioRelayBackend(const GpioRelayBackendConfig &config = GpioRelayBackendConfig())
      : config_(config) {}

  void begin() override;
  void write(const RelayDemand &out) override;

 private:
  GpioRelayBackendConfig config_{};
};

}  // namespace thermostat
