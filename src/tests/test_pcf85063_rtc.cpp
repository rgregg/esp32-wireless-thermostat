#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/pcf85063_rtc.h"
#include "test_harness.h"

using thermostat::RtcTime;
using thermostat::pcf85063_decode;
using thermostat::pcf85063_encode;

TEST_CASE(rtc_bcd_helpers) {
  ASSERT_EQ(thermostat::rtc_bcd_to_dec(0x59), 59u);
  ASSERT_EQ(thermostat::rtc_dec_to_bcd(59), 0x59u);
  ASSERT_EQ(thermostat::rtc_bcd_to_dec(0x00), 0u);
  ASSERT_EQ(thermostat::rtc_dec_to_bcd(0), 0x00u);
}

TEST_CASE(rtc_decode_known_block) {
  // 2026-06-16 14:30:45, weekday 2 (regs 0x04..0x0A)
  uint8_t regs[7] = {0x45, 0x30, 0x14, 0x16, 0x02, 0x06, 0x26};
  RtcTime t;
  bool ok = pcf85063_decode(regs, t);
  ASSERT_TRUE(ok);  // OS flag clear (bit7 of seconds == 0)
  ASSERT_EQ(t.second, 45u);
  ASSERT_EQ(t.minute, 30u);
  ASSERT_EQ(t.hour, 14u);
  ASSERT_EQ(t.day, 16u);
  ASSERT_EQ(t.weekday, 2u);
  ASSERT_EQ(t.month, 6u);
  ASSERT_EQ(t.year, 2026u);
}

TEST_CASE(rtc_decode_os_flag_set_means_untrustworthy) {
  uint8_t regs[7] = {0x80 | 0x12, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00};  // seconds=12 + OS flag
  RtcTime t;
  bool ok = pcf85063_decode(regs, t);
  ASSERT_TRUE(!ok);            // OS flag set -> not trustworthy
  ASSERT_EQ(t.second, 12u);   // but the value is still decoded (mask off bit7)
}

TEST_CASE(rtc_decode_masks_high_bits) {
  // hours reg has stray high bits that must be masked (0x3F); minutes 0x7F; months 0x1F
  uint8_t regs[7] = {0x00, 0xB0 /*min stray*/, 0xC3 /*hr stray->0x03*/, 0x01, 0x00, 0xA9 /*mon stray->0x09*/, 0x99};
  RtcTime t;
  pcf85063_decode(regs, t);
  ASSERT_EQ(t.minute, 30u);   // 0xB0 & 0x7F = 0x30 -> 30
  ASSERT_EQ(t.hour, 3u);      // 0xC3 & 0x3F = 0x03 -> 3
  ASSERT_EQ(t.month, 9u);     // 0xA9 & 0x1F = 0x09 -> 9
  ASSERT_EQ(t.year, 2099u);   // 0x99 -> 99 -> 2099
}

TEST_CASE(rtc_encode_decode_roundtrip) {
  RtcTime in;
  in.year = 2026; in.month = 12; in.day = 31; in.hour = 23; in.minute = 59; in.second = 58; in.weekday = 4;
  uint8_t regs[7];
  pcf85063_encode(in, regs);
  ASSERT_TRUE((regs[0] & 0x80) == 0);  // OS flag cleared on encode
  RtcTime out;
  bool ok = pcf85063_decode(regs, out);
  ASSERT_TRUE(ok);
  ASSERT_EQ(out.year, 2026u);
  ASSERT_EQ(out.month, 12u);
  ASSERT_EQ(out.day, 31u);
  ASSERT_EQ(out.hour, 23u);
  ASSERT_EQ(out.minute, 59u);
  ASSERT_EQ(out.second, 58u);
  ASSERT_EQ(out.weekday, 4u);
}

TEST_CASE(rtc_epoch_known_value) {
  // 2026-06-16 14:30:45 UTC == 1781620245 (verified with `date -u`).
  RtcTime t;
  t.year = 2026; t.month = 6; t.day = 16; t.hour = 14; t.minute = 30; t.second = 45;
  ASSERT_TRUE(thermostat::rtc_time_to_epoch(t) == 1781620245LL);
}

TEST_CASE(rtc_epoch_unix_zero) {
  // Epoch 0 == 1970-01-01 00:00:00 UTC, a Thursday (weekday 4).
  RtcTime t = thermostat::rtc_time_from_epoch(0);
  ASSERT_EQ(t.year, 1970u);
  ASSERT_EQ(t.month, 1u);
  ASSERT_EQ(t.day, 1u);
  ASSERT_EQ(t.hour, 0u);
  ASSERT_EQ(t.minute, 0u);
  ASSERT_EQ(t.second, 0u);
  ASSERT_EQ(t.weekday, 4u);
}

TEST_CASE(rtc_epoch_roundtrip) {
  RtcTime in;
  in.year = 2026; in.month = 6; in.day = 16; in.hour = 14; in.minute = 30; in.second = 45;
  long long epoch = thermostat::rtc_time_to_epoch(in);
  RtcTime out = thermostat::rtc_time_from_epoch(epoch);
  ASSERT_EQ(out.year, in.year);
  ASSERT_EQ(out.month, in.month);
  ASSERT_EQ(out.day, in.day);
  ASSERT_EQ(out.hour, in.hour);
  ASSERT_EQ(out.minute, in.minute);
  ASSERT_EQ(out.second, in.second);
  ASSERT_EQ(out.weekday, 2u);  // 2026-06-16 is a Tuesday
}

TEST_CASE(rtc_epoch_leap_day) {
  // 2028-02-29 (leap day) 12:00:00 UTC round-trips correctly.
  RtcTime in;
  in.year = 2028; in.month = 2; in.day = 29; in.hour = 12; in.minute = 0; in.second = 0;
  RtcTime out = thermostat::rtc_time_from_epoch(thermostat::rtc_time_to_epoch(in));
  ASSERT_EQ(out.year, 2028u);
  ASSERT_EQ(out.month, 2u);
  ASSERT_EQ(out.day, 29u);
  ASSERT_EQ(out.hour, 12u);
}
#endif  // THERMOSTAT_RUN_TESTS
