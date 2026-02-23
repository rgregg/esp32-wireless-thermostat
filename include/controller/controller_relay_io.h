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

  bool has_pending() const { return pending_ != RelaySelect::None; }

  uint32_t pending_wait_remaining_ms(uint32_t now_ms) const {
    if (pending_ == RelaySelect::None) return 0;
    const uint32_t wait = wait_ms_for(pending_);
    const uint32_t elapsed = now_ms - pending_since_ms_;
    return (elapsed < wait) ? (wait - elapsed) : 0;
  }

  const char *pending_name() const {
    switch (pending_) {
      case RelaySelect::Heat: return "HEAT";
      case RelaySelect::Cool: return "COOL";
      case RelaySelect::Fan: return "FAN";
      case RelaySelect::Spare: return "SPARE";
      default: return "NONE";
    }
  }

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

