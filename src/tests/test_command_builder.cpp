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

#endif
