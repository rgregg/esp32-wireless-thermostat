#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/controller_relay_io.h"
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

TEST_CASE(controller_relay_io_pending_accessors_during_interlock) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.heat_interlock_wait_ms = 500;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  // Initially no pending state
  ASSERT_TRUE(!io.has_pending());
  ASSERT_EQ(io.pending_wait_remaining_ms(0), 0u);
  ASSERT_STR_EQ(io.pending_name(), "NONE");

  // Activate heat
  RelayDemand heat;
  heat.heat = true;
  io.apply(100, heat, false);
  ASSERT_TRUE(io.latched_output().heat);
  ASSERT_TRUE(!io.has_pending());

  // Switch to cool — enters interlock
  RelayDemand cool;
  cool.cool = true;
  io.apply(800, cool, false);
  ASSERT_TRUE(io.has_pending());
  ASSERT_STR_EQ(io.pending_name(), "COOL");

  // Remaining time should be close to full wait (1000ms default for cool)
  uint32_t remaining = io.pending_wait_remaining_ms(800);
  ASSERT_TRUE(remaining == 1000);

  // Midway through interlock
  remaining = io.pending_wait_remaining_ms(1300);
  ASSERT_TRUE(remaining == 500);

  // After interlock expires, apply clears pending
  io.apply(1801, cool, false);
  ASSERT_TRUE(!io.has_pending());
  ASSERT_TRUE(io.latched_output().cool);
  ASSERT_STR_EQ(io.pending_name(), "NONE");
}

TEST_CASE(controller_relay_io_force_off_cancels_pending) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  // Activate heat, then request cool to start interlock
  RelayDemand heat;
  heat.heat = true;
  io.apply(0, heat, false);
  ASSERT_TRUE(io.latched_output().heat);

  RelayDemand cool;
  cool.cool = true;
  io.apply(100, cool, false);
  ASSERT_TRUE(io.has_pending());

  // Force-off should clear pending state immediately
  io.apply(200, cool, true);
  ASSERT_TRUE(!io.has_pending());
  ASSERT_TRUE(!io.latched_output().heat);
  ASSERT_TRUE(!io.latched_output().cool);
}

TEST_CASE(controller_relay_io_same_relay_no_interlock) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  // Activate heat
  RelayDemand heat;
  heat.heat = true;
  io.apply(0, heat, false);
  ASSERT_TRUE(io.latched_output().heat);

  // Re-request the same relay — should stay on, no interlock
  io.apply(100, heat, false);
  ASSERT_TRUE(io.latched_output().heat);
  ASSERT_TRUE(!io.has_pending());
}

TEST_CASE(controller_relay_io_demand_change_mid_interlock) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.heat_interlock_wait_ms = 500;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  // Start with heat active
  RelayDemand heat;
  heat.heat = true;
  io.apply(0, heat, false);
  ASSERT_TRUE(io.latched_output().heat);

  // Request cool — enters interlock for cool (1000ms)
  RelayDemand cool;
  cool.cool = true;
  io.apply(100, cool, false);
  ASSERT_TRUE(io.has_pending());
  ASSERT_STR_EQ(io.pending_name(), "COOL");

  // Mid-interlock, change mind back to heat — restarts interlock for heat (500ms)
  io.apply(300, heat, false);
  ASSERT_TRUE(io.has_pending());
  ASSERT_STR_EQ(io.pending_name(), "HEAT");

  // Old cool wait (1000ms from t=100) would expire at t=1100, but heat wait
  // is 500ms from t=300 = t=800. At t=700, still waiting.
  io.apply(700, heat, false);
  ASSERT_TRUE(!io.latched_output().heat);
  ASSERT_TRUE(io.has_pending());

  // At t=801, heat interlock complete
  io.apply(801, heat, false);
  ASSERT_TRUE(io.latched_output().heat);
  ASSERT_TRUE(!io.has_pending());
}

TEST_CASE(controller_relay_io_heat_uses_shorter_interlock) {
  thermostat::ControllerRelayIoConfig cfg;
  cfg.heat_interlock_wait_ms = 500;
  cfg.default_interlock_wait_ms = 1000;
  thermostat::ControllerRelayIo io(cfg);
  io.begin();

  // Start with cool active
  RelayDemand cool;
  cool.cool = true;
  io.apply(0, cool, false);

  // Switch to heat — should use 500ms (heat_interlock_wait_ms), not 1000ms
  RelayDemand heat;
  heat.heat = true;
  io.apply(100, heat, false);
  ASSERT_TRUE(io.has_pending());

  // At 100 + 499 = 599, should still be waiting
  io.apply(599, heat, false);
  ASSERT_TRUE(!io.latched_output().heat);

  // At 100 + 500 = 600, should be active
  io.apply(601, heat, false);
  ASSERT_TRUE(io.latched_output().heat);
}
#endif
