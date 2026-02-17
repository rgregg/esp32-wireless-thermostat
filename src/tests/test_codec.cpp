#if defined(THERMOSTAT_RUN_TESTS)
#include "espnow_cmd_word.h"
#include "test_harness.h"

TEST_CASE(codec_roundtrip) {
  CommandWord cmd;
  cmd.mode = FurnaceMode::Heat;
  cmd.fan = FanMode::Circulate;
  cmd.setpoint_decic = 205;
  cmd.seq = 123;
  cmd.filter_reset = true;
  cmd.sync_request = false;

  const uint32_t packed = espnow_cmd::encode(cmd);
  const CommandWord out = espnow_cmd::decode(packed);

  ASSERT_EQ(static_cast<int>(out.mode), static_cast<int>(cmd.mode));
  ASSERT_EQ(static_cast<int>(out.fan), static_cast<int>(cmd.fan));
  ASSERT_EQ(out.setpoint_decic, cmd.setpoint_decic);
  ASSERT_EQ(out.seq, cmd.seq);
  ASSERT_EQ(out.filter_reset, true);
  ASSERT_EQ(out.sync_request, false);
}

TEST_CASE(codec_sequence_newer_logic) {
  ASSERT_TRUE(espnow_cmd::is_newer_seq(10, 11));
  ASSERT_TRUE(!espnow_cmd::is_newer_seq(10, 10));
  ASSERT_TRUE(espnow_cmd::is_newer_seq(510, 1));
  ASSERT_TRUE(!espnow_cmd::is_newer_seq(100, 400));
}
#endif
