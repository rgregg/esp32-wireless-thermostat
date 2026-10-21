#if defined(THERMOSTAT_RUN_TESTS)
#include "command_builder.h"
#include "test_harness.h"

#include <math.h>

TEST_CASE(command_builder_builds_expected_command_word) {
  const CommandWord cmd = thermostat::build_command_word(
      FurnaceMode::Cool, FanMode::Circulate, 21.25f, 513, true, false);

  ASSERT_EQ(static_cast<int>(cmd.mode), static_cast<int>(FurnaceMode::Cool));
  ASSERT_EQ(static_cast<int>(cmd.fan), static_cast<int>(FanMode::Circulate));
  ASSERT_EQ(cmd.setpoint_decic, static_cast<uint16_t>(213));
  ASSERT_EQ(cmd.seq, static_cast<uint16_t>(1));
  ASSERT_TRUE(cmd.sync_request);
  ASSERT_TRUE(!cmd.filter_reset);
}

TEST_CASE(command_builder_clamps_setpoint_and_matches_codec) {
  const uint32_t packed = thermostat::build_packed_command(
      FurnaceMode::Heat, FanMode::AlwaysOn, 99.9f, 11, false, true);
  const CommandWord decoded = espnow_cmd::decode(packed);

  ASSERT_EQ(static_cast<int>(decoded.mode), static_cast<int>(FurnaceMode::Heat));
  ASSERT_EQ(static_cast<int>(decoded.fan), static_cast<int>(FanMode::AlwaysOn));
  ASSERT_EQ(decoded.setpoint_decic, static_cast<uint16_t>(400));
  ASSERT_EQ(decoded.seq, static_cast<uint16_t>(11));
  ASSERT_TRUE(!decoded.sync_request);
  ASSERT_TRUE(decoded.filter_reset);
}

TEST_CASE(command_builder_guards_nan_and_inf_setpoint) {
  // NaN setpoint should produce 0 (not undefined behavior)
  const float nan_val = nanf("");
  const CommandWord nan_cmd = thermostat::build_command_word(
      FurnaceMode::Heat, FanMode::Automatic, nan_val, 1, false, false);
  ASSERT_EQ(nan_cmd.setpoint_decic, static_cast<uint16_t>(0));

  // +Inf setpoint: isfinite() maps to 0, then clamps to 0
  const float inf_val = HUGE_VALF;
  const CommandWord inf_cmd = thermostat::build_command_word(
      FurnaceMode::Heat, FanMode::Automatic, inf_val, 1, false, false);
  ASSERT_EQ(inf_cmd.setpoint_decic, static_cast<uint16_t>(0));

  // -Inf setpoint: isfinite() maps to 0, then clamps to 0
  const CommandWord neg_inf_cmd = thermostat::build_command_word(
      FurnaceMode::Heat, FanMode::Automatic, -HUGE_VALF, 1, false, false);
  ASSERT_EQ(neg_inf_cmd.setpoint_decic, static_cast<uint16_t>(0));
}

TEST_CASE(command_word_session_roundtrips_in_spare_bits) {
  CommandWord cmd;
  cmd.seq = 0x1FF;
  cmd.preserve_setpoint = true;
  cmd.session = 31;
  const uint32_t packed = espnow_cmd::encode(cmd);
  const CommandWord decoded = espnow_cmd::decode(packed);
  ASSERT_EQ(decoded.session, static_cast<uint8_t>(31));
  ASSERT_EQ(decoded.seq, static_cast<uint16_t>(0x1FF));
  ASSERT_TRUE(decoded.preserve_setpoint);
  cmd.session = 0;  // a session-less word is bit-identical to the old format
  ASSERT_EQ(espnow_cmd::encode(cmd) >> 27, 0u);
  ASSERT_EQ(espnow_cmd::next_session(31), static_cast<uint8_t>(1));
  ASSERT_EQ(espnow_cmd::next_session(0), static_cast<uint8_t>(1));
}

#endif
