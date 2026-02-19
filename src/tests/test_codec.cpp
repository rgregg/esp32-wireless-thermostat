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
  ASSERT_TRUE(!espnow_cmd::is_newer_seq(100, 357));  // diff == 0x101 (too old)
  ASSERT_TRUE(espnow_cmd::is_newer_seq(100, 356));   // diff == 0x100 (max accepted)
}

TEST_CASE(codec_next_seq_rollover) {
  ASSERT_EQ(espnow_cmd::next_seq(0), static_cast<uint16_t>(1));
  ASSERT_EQ(espnow_cmd::next_seq(510), static_cast<uint16_t>(511));
  ASSERT_EQ(espnow_cmd::next_seq(511), static_cast<uint16_t>(0));
}

TEST_CASE(codec_encode_decode_clamps_and_masks_fields) {
  CommandWord cmd;
  cmd.mode = FurnaceMode::Cool;
  cmd.fan = FanMode::AlwaysOn;
  cmd.setpoint_decic = 999;  // should clamp to 400
  cmd.seq = 0x3FF;           // should mask to 0x1FF
  cmd.filter_reset = true;
  cmd.sync_request = true;

  const uint32_t packed = espnow_cmd::encode(cmd);
  const CommandWord out = espnow_cmd::decode(packed);
  ASSERT_EQ(static_cast<int>(out.mode), static_cast<int>(FurnaceMode::Cool));
  ASSERT_EQ(static_cast<int>(out.fan), static_cast<int>(FanMode::AlwaysOn));
  ASSERT_EQ(out.setpoint_decic, static_cast<uint16_t>(400));
  ASSERT_EQ(out.seq, static_cast<uint16_t>(0x1FF));
  ASSERT_TRUE(out.filter_reset);
  ASSERT_TRUE(out.sync_request);
}
#endif
