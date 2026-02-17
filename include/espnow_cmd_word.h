#pragma once

#include <stdint.h>

#include "thermostat_types.h"

struct CommandWord {
  FurnaceMode mode = FurnaceMode::Off;
  FanMode fan = FanMode::Automatic;
  uint16_t setpoint_decic = 0;  // 0..400
  uint16_t seq = 0;             // 9 bits, 0..511
  bool filter_reset = false;
  bool sync_request = false;
};

namespace espnow_cmd {

uint16_t next_seq(uint16_t current_seq);
uint32_t encode(const CommandWord &cmd);
CommandWord decode(uint32_t packed);

// Same stale logic used by ESPHome controller:
// - diff == 0 => duplicate
// - diff > 0x100 => stale (backward in 9-bit ring)
// Valid "new" range is 1..0x100.
bool is_newer_seq(uint16_t last_seq, uint16_t incoming_seq);

}  // namespace espnow_cmd
