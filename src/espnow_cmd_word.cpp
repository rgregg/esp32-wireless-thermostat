#include "espnow_cmd_word.h"

namespace {
constexpr uint16_t kSeqMask = 0x1FF;
constexpr uint16_t kSetpointMaxDeciC = 400;
}  // namespace

namespace espnow_cmd {

uint16_t next_seq(uint16_t current_seq) { return (current_seq + 1) & kSeqMask; }

uint32_t encode(const CommandWord &cmd) {
  const uint16_t sp10 = (cmd.setpoint_decic > kSetpointMaxDeciC)
                            ? kSetpointMaxDeciC
                            : cmd.setpoint_decic;
  const uint16_t seq = cmd.seq & kSeqMask;

  uint32_t w = 0;
  w |= (static_cast<uint32_t>(cmd.mode) & 0x3u) << 0;
  w |= (static_cast<uint32_t>(cmd.fan) & 0x3u) << 2;
  w |= (static_cast<uint32_t>(sp10) & 0x1FFu) << 4;
  w |= (static_cast<uint32_t>(seq) & 0x1FFu) << 13;
  w |= (cmd.filter_reset ? 1u : 0u) << 22;
  w |= (cmd.sync_request ? 1u : 0u) << 23;
  w |= (cmd.preserve_mode ? 1u : 0u) << 24;
  w |= (cmd.preserve_fan ? 1u : 0u) << 25;
  w |= (cmd.preserve_setpoint ? 1u : 0u) << 26;
  return w;
}

CommandWord decode(uint32_t packed) {
  CommandWord cmd;
  cmd.mode = static_cast<FurnaceMode>((packed >> 0) & 0x3u);
  cmd.fan = static_cast<FanMode>((packed >> 2) & 0x3u);
  cmd.setpoint_decic = static_cast<uint16_t>((packed >> 4) & 0x1FFu);
  if (cmd.setpoint_decic > kSetpointMaxDeciC) {
    cmd.setpoint_decic = kSetpointMaxDeciC;
  }
  cmd.seq = static_cast<uint16_t>((packed >> 13) & 0x1FFu);
  cmd.filter_reset = ((packed >> 22) & 0x1u) != 0;
  cmd.sync_request = ((packed >> 23) & 0x1u) != 0;
  cmd.preserve_mode = ((packed >> 24) & 0x1u) != 0;
  cmd.preserve_fan = ((packed >> 25) & 0x1u) != 0;
  cmd.preserve_setpoint = ((packed >> 26) & 0x1u) != 0;
  return cmd;
}

bool is_newer_seq(uint16_t last_seq, uint16_t incoming_seq) {
  const uint16_t diff = (incoming_seq - last_seq) & kSeqMask;
  return diff != 0 && diff <= 0x100;
}

}  // namespace espnow_cmd
