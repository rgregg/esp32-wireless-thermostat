#pragma once

#include <string>

#include "thermostat_types.h"

namespace thermostat {

std::string furnace_state_text(FurnaceStateCode state,
                               bool connected,
                               bool lockout,
                               bool failsafe_active);

std::string furnace_mode_text(FurnaceMode mode);
std::string fan_mode_text(FanMode mode);

}  // namespace thermostat
