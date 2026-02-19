#pragma once

#include <stdint.h>

#include "espnow_cmd_word.h"

namespace thermostat {

CommandWord build_command_word(FurnaceMode mode,
                               FanMode fan,
                               float setpoint_c,
                               uint16_t seq,
                               bool sync_request,
                               bool filter_reset);

uint32_t build_packed_command(FurnaceMode mode,
                              FanMode fan,
                              float setpoint_c,
                              uint16_t seq,
                              bool sync_request,
                              bool filter_reset);

}  // namespace thermostat
