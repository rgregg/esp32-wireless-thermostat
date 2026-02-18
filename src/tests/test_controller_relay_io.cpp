#if defined(THERMOSTAT_RUN_TESTS)
#include "controller_relay_io.h"
#include "test_harness.h"

TEST_CASE(controller_relay_io_interlock_waits_before_switch) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.heat_interlock_wait_ms = 500;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  RelayDemand heat;
  heat.heat = true;
  io.apply(100, heat, false);
  ASSERT_TRUE(io.latched_output().heat);

  RelayDemand cool;
  cool.cool = true;
  io.apply(800, cool, false);
  ASSERT_TRUE(!io.latched_output().heat);
  ASSERT_TRUE(!io.latched_output().cool);

  io.apply(1800, cool, false);
  ASSERT_TRUE(io.latched_output().cool);
}

TEST_CASE(controller_relay_io_force_off_clears_immediately) {
  thermostat::ControllerRelayIo io;
  io.begin();

  RelayDemand fan;
  fan.fan = true;
  io.apply(0, fan, false);
  io.apply(1000, fan, false);
  ASSERT_TRUE(io.latched_output().fan);

  io.apply(1001, fan, true);
  ASSERT_TRUE(!io.latched_output().heat);
  ASSERT_TRUE(!io.latched_output().cool);
  ASSERT_TRUE(!io.latched_output().fan);
  ASSERT_TRUE(!io.latched_output().spare);
}
#endif
