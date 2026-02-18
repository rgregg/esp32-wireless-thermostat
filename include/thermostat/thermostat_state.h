#pragma once

#include <stdint.h>

#include "thermostat_types.h"

namespace thermostat {

FurnaceStateCode compute_furnace_state(const ThermostatSnapshot &snapshot);

// Returns true when controller should enter failsafe due to heartbeat timeout.
bool is_failsafe_timed_out(uint32_t now_ms, uint32_t heartbeat_last_seen_ms,
                           uint32_t timeout_ms);

}  // namespace thermostat
