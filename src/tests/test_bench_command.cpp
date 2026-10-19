#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "thermostat/bench_command.h"

using thermostat::BenchCommand;
using thermostat::parse_bench_command;

TEST_CASE(bench_command_parses_all_fields_space_separated) {
  BenchCommand c;
  const char *err = nullptr;
  ASSERT_TRUE(parse_bench_command("mode=heat fan=on sp=22.5", &c, &err));
  ASSERT_TRUE(c.has_mode && c.mode == FurnaceMode::Heat);
  ASSERT_TRUE(c.has_fan && c.fan == FanMode::AlwaysOn);
  ASSERT_TRUE(c.has_setpoint && c.setpoint_c == 22.5f);
  ASSERT_TRUE(!c.sync);
}

TEST_CASE(bench_command_accepts_form_separators_and_partial_fields) {
  BenchCommand c;
  const char *err = nullptr;
  ASSERT_TRUE(parse_bench_command("fan=circulate&mode=cool", &c, &err));
  ASSERT_TRUE(c.has_fan && c.fan == FanMode::Circulate);
  ASSERT_TRUE(c.has_mode && c.mode == FurnaceMode::Cool);
  ASSERT_TRUE(!c.has_setpoint);
  ASSERT_TRUE(parse_bench_command("sync=1\r\n", &c, &err));
  ASSERT_TRUE(c.sync && !c.has_mode && !c.has_fan && !c.has_setpoint);
}

TEST_CASE(bench_command_rejects_bad_input_and_leaves_output_untouched) {
  BenchCommand c;
  c.has_mode = true;
  c.mode = FurnaceMode::Heat;
  const char *err = nullptr;
  ASSERT_TRUE(!parse_bench_command("", &c, &err));
  ASSERT_TRUE(!parse_bench_command("mode=dry", &c, &err));
  ASSERT_TRUE(!parse_bench_command("sp=41", &c, &err));
  ASSERT_TRUE(!parse_bench_command("sp=abc", &c, &err));
  ASSERT_TRUE(!parse_bench_command("sp=22x", &c, &err));
  ASSERT_TRUE(!parse_bench_command("mode=", &c, &err));
  ASSERT_TRUE(!parse_bench_command("volume=11", &c, &err));
  ASSERT_TRUE(!parse_bench_command("mode=off heat", &c, &err));
  ASSERT_TRUE(!parse_bench_command(nullptr, &c, &err));
  ASSERT_TRUE(err != nullptr && err[0] != '\0');
  ASSERT_TRUE(c.has_mode && c.mode == FurnaceMode::Heat);  // a failed parse writes nothing
}

TEST_CASE(bench_command_rejects_overlong_line) {
  BenchCommand c;
  const char *err = nullptr;
  char line[200];
  memset(line, ' ', sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  memcpy(line, "mode=heat", 9);
  ASSERT_TRUE(!parse_bench_command(line, &c, &err));
}

#endif
