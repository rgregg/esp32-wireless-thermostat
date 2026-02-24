#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/controller_runtime.h"
#include "test_harness.h"

TEST_CASE(controller_runtime_failsafe_lockout) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 5000;
  cfg.min_idle_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  thermostat::ControllerRuntime rt(cfg);

  rt.note_heartbeat(1000);
  thermostat::ControllerTickInput t1;
  t1.now_ms = 2000;
  t1.cool_call = true;
  rt.tick(t1);
  ASSERT_TRUE(rt.cool_demand());

  thermostat::ControllerTickInput t2;
  t2.now_ms = 8000;
  t2.cool_call = true;
  rt.tick(t2);
  ASSERT_TRUE(rt.failsafe_active());
  ASSERT_TRUE(!rt.cool_demand());

  rt.note_heartbeat(9000);
  thermostat::ControllerTickInput t3;
  t3.now_ms = 9001;
  rt.tick(t3);
  ASSERT_TRUE(!rt.failsafe_active());

  rt.set_hvac_lockout(true);
  thermostat::ControllerTickInput t4;
  t4.now_ms = 9100;
  t4.heat_call = true;
  rt.tick(t4);
  ASSERT_TRUE(!rt.heat_demand());
}

TEST_CASE(controller_runtime_filter_runtime_and_circulate) {
  thermostat::ControllerConfig cfg;
  cfg.fan_circulate_period_min = 5;
  cfg.fan_circulate_duration_min = 2;
  cfg.min_idle_time_ms = 0;
  cfg.min_heating_off_time_ms = 0;
  cfg.min_heating_run_time_ms = 0;
  cfg.min_cooling_off_time_ms = 0;
  cfg.min_cooling_run_time_ms = 0;
  thermostat::ControllerRuntime rt(cfg);

  CommandWord cmd;
  cmd.fan = FanMode::Circulate;
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);

  rt.note_heartbeat(1);
  thermostat::ControllerTickInput first;
  first.now_ms = 1000;
  rt.tick(first);

  thermostat::ControllerTickInput second;
  second.now_ms = 61000;
  rt.tick(second);

  ASSERT_TRUE(rt.fan_demand());
  thermostat::ControllerTickInput third;
  third.now_ms = 121000;
  rt.tick(third);
  ASSERT_TRUE(rt.filter_runtime_seconds() >= 60);
}

TEST_CASE(controller_runtime_enforces_min_run_min_off_and_min_idle) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 1000000;
  cfg.min_idle_time_ms = 30000;
  cfg.min_heating_run_time_ms = 180000;
  cfg.min_heating_off_time_ms = 180000;
  cfg.min_cooling_run_time_ms = 300000;
  cfg.min_cooling_off_time_ms = 300000;
  thermostat::ControllerRuntime rt(cfg);

  rt.note_heartbeat(1);

  thermostat::ControllerTickInput t1;
  t1.now_ms = 1000;
  t1.heat_call = true;
  rt.tick(t1);
  ASSERT_TRUE(!rt.heat_demand());

  thermostat::ControllerTickInput t2;
  t2.now_ms = 31000;
  t2.heat_call = true;
  rt.tick(t2);
  ASSERT_TRUE(rt.heat_demand());

  thermostat::ControllerTickInput t3;
  t3.now_ms = 60000;
  t3.heat_call = false;
  rt.tick(t3);
  ASSERT_TRUE(rt.heat_demand());

  thermostat::ControllerTickInput t4;
  t4.now_ms = 211000;
  t4.heat_call = false;
  rt.tick(t4);
  ASSERT_TRUE(!rt.heat_demand());

  thermostat::ControllerTickInput t5;
  t5.now_ms = 220000;
  t5.heat_call = true;
  rt.tick(t5);
  ASSERT_TRUE(!rt.heat_demand());

  thermostat::ControllerTickInput t6;
  t6.now_ms = 392000;
  t6.heat_call = true;
  rt.tick(t6);
  ASSERT_TRUE(rt.heat_demand());
}

TEST_CASE(controller_runtime_reset_remote_command_sequence_allows_reseed) {
  thermostat::ControllerRuntime rt;

  CommandWord cmd;
  cmd.seq = 100;
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);

  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd).stale_or_duplicate);

  rt.reset_remote_command_sequence();
  ASSERT_TRUE(rt.apply_remote_command(cmd).accepted);
}

TEST_CASE(controller_runtime_per_source_sequence_tracking) {
  thermostat::ControllerRuntime rt;

  // Two different source MACs
  const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  const uint8_t mac_b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

  // Source A sends seq=1
  CommandWord cmd;
  cmd.seq = 1;
  cmd.mode = FurnaceMode::Heat;
  cmd.setpoint_decic = 210;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_a).accepted);

  // Source B also sends seq=1 — should be accepted (independent sequence)
  cmd.seq = 1;
  cmd.mode = FurnaceMode::Cool;
  cmd.setpoint_decic = 240;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_b).accepted);

  // Source A sends seq=2 — accepted
  cmd.seq = 2;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_a).accepted);

  // Source A sends seq=1 again — stale
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_a).stale_or_duplicate);

  // Source B sends seq=2 — accepted
  cmd.seq = 2;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_b).accepted);

  // Reset clears all sources
  rt.reset_remote_command_sequence();
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_a).accepted);
  cmd.seq = 1;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_b).accepted);
}

TEST_CASE(controller_runtime_null_source_uses_default_sequence) {
  thermostat::ControllerRuntime rt;

  CommandWord cmd;
  cmd.seq = 5;
  ASSERT_TRUE(rt.apply_remote_command(cmd, nullptr).accepted);

  // Same seq without source — duplicate
  ASSERT_TRUE(rt.apply_remote_command(cmd, nullptr).stale_or_duplicate);

  // Different source MAC with same seq — accepted (independent)
  const uint8_t mac_a[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  cmd.seq = 5;
  ASSERT_TRUE(rt.apply_remote_command(cmd, mac_a).accepted);
}
#endif
