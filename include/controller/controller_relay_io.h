#pragma once

#include <stdint.h>

#include "thermostat_types.h"

namespace thermostat {

struct ControllerRelayIoConfig {
  int heat_pin = 32;
  int cool_pin = 33;
  int fan_pin = 25;
  int spare_pin = 26;
  bool inverted = false;
  uint32_t heat_interlock_wait_ms = 500;
  uint32_t default_interlock_wait_ms = 1000;
};

class ControllerRelayIo {
 public:
  explicit ControllerRelayIo(const ControllerRelayIoConfig &config = ControllerRelayIoConfig())
      : config_(config) {}

  void begin();
  void apply(uint32_t now_ms, const RelayDemand &desired, bool force_off);

  const RelayDemand &latched_output() const { return output_; }

 private:
  enum class RelaySelect : uint8_t {
    None = 0,
    Heat = 1,
    Cool = 2,
    Fan = 3,
    Spare = 4,
  };

  static RelaySelect pick_single(const RelayDemand &demand);
  uint32_t wait_ms_for(RelaySelect relay) const;
  RelayDemand to_demand(RelaySelect relay) const;
  void write_outputs(const RelayDemand &out);

  ControllerRelayIoConfig config_{};
  bool initialized_ = false;
  RelayDemand output_{};
  RelaySelect active_ = RelaySelect::None;
  RelaySelect pending_ = RelaySelect::None;
  uint32_t pending_since_ms_ = 0;
};

}  // namespace thermostat

