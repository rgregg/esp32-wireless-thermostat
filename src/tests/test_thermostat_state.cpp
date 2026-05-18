#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat/thermostat_state.h"
#include "test_harness.h"

namespace {

ThermostatSnapshot make_snapshot(FurnaceMode mode) {
  ThermostatSnapshot s;
  s.mode = mode;
  return s;
}

}  // namespace

// --- compute_furnace_state: relay precedence ---

TEST_CASE(thermostat_state_heat_relay_reports_heat_on) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Heat);
  s.relay.heat = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::HeatOn);
}

TEST_CASE(thermostat_state_cool_relay_reports_cool_on) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Cool);
  s.relay.cool = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::CoolOn);
}

TEST_CASE(thermostat_state_fan_relay_reports_fan_on) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Off);
  s.relay.fan = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::FanOn);
}

TEST_CASE(thermostat_state_heat_relay_wins_over_mode) {
  // If the heat relay is energized we report HeatOn regardless of mode —
  // protects against UI lying about what the controller is actually doing.
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Cool);
  s.relay.heat = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::HeatOn);
}

TEST_CASE(thermostat_state_heat_relay_wins_over_cool_relay) {
  // Both relays should never be on simultaneously, but if they are, heat is
  // the safer thing to report (it's more visible / more concerning).
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Heat);
  s.relay.heat = true;
  s.relay.cool = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::HeatOn);
}

TEST_CASE(thermostat_state_cool_relay_wins_over_fan_relay) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Cool);
  s.relay.cool = true;
  s.relay.fan = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::CoolOn);
}

// --- compute_furnace_state: failsafe / lockout override everything ---

TEST_CASE(thermostat_state_failsafe_returns_error_even_with_relays) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Heat);
  s.relay.heat = true;
  s.failsafe_active = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::Error);
}

TEST_CASE(thermostat_state_lockout_returns_error_even_with_relays) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Cool);
  s.relay.cool = true;
  s.hvac_lockout = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::Error);
}

TEST_CASE(thermostat_state_failsafe_returns_error_when_idle) {
  ThermostatSnapshot s = make_snapshot(FurnaceMode::Off);
  s.failsafe_active = true;
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::Error);
}

// --- compute_furnace_state: mode mapping when idle ---

TEST_CASE(thermostat_state_idle_heat_mode_reports_heat_standby) {
  ASSERT_TRUE(thermostat::compute_furnace_state(make_snapshot(FurnaceMode::Heat)) ==
              FurnaceStateCode::HeatMode);
}

TEST_CASE(thermostat_state_idle_cool_mode_reports_cool_standby) {
  ASSERT_TRUE(thermostat::compute_furnace_state(make_snapshot(FurnaceMode::Cool)) ==
              FurnaceStateCode::CoolMode);
}

TEST_CASE(thermostat_state_idle_off_mode_reports_idle) {
  ASSERT_TRUE(thermostat::compute_furnace_state(make_snapshot(FurnaceMode::Off)) ==
              FurnaceStateCode::Idle);
}

TEST_CASE(thermostat_state_unknown_mode_reports_error) {
  ThermostatSnapshot s;
  s.mode = static_cast<FurnaceMode>(99);
  ASSERT_TRUE(thermostat::compute_furnace_state(s) == FurnaceStateCode::Error);
}

// --- is_failsafe_timed_out ---

TEST_CASE(thermostat_state_failsafe_timeout_with_no_heartbeat_yet) {
  // heartbeat_last_seen_ms == 0 means we've never heard from the peer.
  // Failsafe should trip once now exceeds the timeout.
  ASSERT_TRUE(thermostat::is_failsafe_timed_out(10000, 0, 5000));
  ASSERT_TRUE(!thermostat::is_failsafe_timed_out(3000, 0, 5000));
}

TEST_CASE(thermostat_state_failsafe_not_timed_out_when_recent) {
  // 1s since last heartbeat, 5s timeout → not yet expired.
  ASSERT_TRUE(!thermostat::is_failsafe_timed_out(11000, 10000, 5000));
}

TEST_CASE(thermostat_state_failsafe_timed_out_when_stale) {
  // 6s since last heartbeat, 5s timeout → expired.
  ASSERT_TRUE(thermostat::is_failsafe_timed_out(16000, 10000, 5000));
}

TEST_CASE(thermostat_state_failsafe_boundary_exact_timeout_not_expired) {
  // Implementation uses strict greater-than, so equal-to-timeout is OK.
  ASSERT_TRUE(!thermostat::is_failsafe_timed_out(15000, 10000, 5000));
}

TEST_CASE(thermostat_state_failsafe_handles_millis_wraparound) {
  // millis() wraps at 2^32 ms (~49.7 days). With unsigned subtraction the
  // delta computes correctly across the wrap. Last heartbeat just before
  // wrap, "now" just after: only 100 ms have actually elapsed.
  const uint32_t before_wrap = 0xFFFFFF00u;
  const uint32_t after_wrap = 0x0000004Cu;  // 0xFFFFFF00 + 0x14C = wrap + 0x4C
  ASSERT_TRUE(!thermostat::is_failsafe_timed_out(after_wrap, before_wrap, 5000));
}

#endif
