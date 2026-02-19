#include "command_builder.h"

namespace thermostat {

CommandWord build_command_word(FurnaceMode mode,
                               FanMode fan,
                               float setpoint_c,
                               uint16_t seq,
                               bool sync_request,
                               bool filter_reset) {
  CommandWord cmd;
  cmd.mode = mode;
  cmd.fan = fan;

  int setpoint_decic = static_cast<int>(setpoint_c * 10.0f + 0.5f);
  if (setpoint_decic < 0) {
    setpoint_decic = 0;
  }
  if (setpoint_decic > 400) {
    setpoint_decic = 400;
  }
  cmd.setpoint_decic = static_cast<uint16_t>(setpoint_decic);
  cmd.seq = seq & 0x1FFu;
  cmd.sync_request = sync_request;
  cmd.filter_reset = filter_reset;
  return cmd;
}

uint32_t build_packed_command(FurnaceMode mode,
                              FanMode fan,
                              float setpoint_c,
                              uint16_t seq,
                              bool sync_request,
                              bool filter_reset) {
  return espnow_cmd::encode(
      build_command_word(mode, fan, setpoint_c, seq, sync_request, filter_reset));
}

}  // namespace thermostat
