#if defined(THERMOSTAT_RUN_TESTS)
#include "controller_runtime.h"
#include "test_harness.h"

TEST_CASE(controller_runtime_failsafe_lockout) {
  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 5000;
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
#endif
