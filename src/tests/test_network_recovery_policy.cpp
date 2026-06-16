#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/network_recovery_policy.h"
#include "test_harness.h"

using thermostat::NetworkRecoveryPolicy;
using thermostat::NetworkRecoveryConfig;
using thermostat::RecoveryAction;

static NetworkRecoveryConfig cfg() {
  NetworkRecoveryConfig c;
  c.base_backoff_ms = 1000;
  c.max_backoff_ms = 8000;
  c.fails_before_restart = 3;
  c.restarts_before_reboot = 2;
  return c;
}

TEST_CASE(recovery_first_poll_attempts_connect_immediately) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  ASSERT_TRUE(p.poll(1) == RecoveryAction::None);
}

TEST_CASE(recovery_backoff_doubles_and_caps) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  p.on_connect_failed();
  ASSERT_TRUE(p.poll(500) == RecoveryAction::None);
  ASSERT_TRUE(p.poll(1000) == RecoveryAction::Connect);
  ASSERT_EQ(p.current_backoff_ms(), 1000u);
  p.on_connect_failed();
  ASSERT_EQ(p.current_backoff_ms(), 2000u);
}

TEST_CASE(recovery_escalates_to_restart_then_reboot) {
  NetworkRecoveryPolicy p(cfg());
  uint32_t t = 0;
  int restarts = 0, reboots = 0, guard = 0;
  while (guard++ < 1000 && reboots == 0) {
    RecoveryAction a = p.poll(t);
    if (a == RecoveryAction::Connect) { p.on_connect_failed(); }
    else if (a == RecoveryAction::RestartSubsystem) { restarts++; }
    else if (a == RecoveryAction::Reboot) { reboots++; }
    t += 10000;
  }
  ASSERT_EQ(restarts, 2);
  ASSERT_EQ(reboots, 1);
}

TEST_CASE(recovery_on_connected_resets_everything) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  p.on_connect_failed();
  p.poll(2000); p.on_connect_failed();
  ASSERT_TRUE(p.consecutive_fails() > 0);
  p.on_connected();
  ASSERT_TRUE(p.connected());
  ASSERT_EQ(p.consecutive_fails(), 0u);
  ASSERT_EQ(p.restart_count(), 0u);
  ASSERT_EQ(p.current_backoff_ms(), 0u);
}

TEST_CASE(recovery_disconnect_resumes_connect_cycle_without_escalating) {
  NetworkRecoveryPolicy p(cfg());
  p.poll(0); p.on_connected();
  p.on_disconnected();
  ASSERT_TRUE(!p.connected());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  ASSERT_EQ(p.restart_count(), 0u);
}
#endif  // THERMOSTAT_RUN_TESTS
