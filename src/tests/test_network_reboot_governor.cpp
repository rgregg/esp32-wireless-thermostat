#if defined(THERMOSTAT_RUN_TESTS)
#include "network_reboot_governor.h"
#include "test_harness.h"

using thermostat::GovernorAction;
using thermostat::NetworkRebootConfig;
using thermostat::NetworkRebootGovernor;

namespace {
constexpr uint32_t kMin = 60UL * 1000UL;
constexpr uint32_t kHour = 60UL * kMin;

struct Outcome {
  uint32_t recover_at = 0;
  uint32_t reboot_at = 0;
  int recovers = 0;
  int reboots = 0;
};

// Polls every second while isolated, from `start` for `duration`.
Outcome run_isolated(NetworkRebootGovernor &g, uint32_t start, uint32_t duration,
                     bool network_was_ready = true) {
  Outcome o;
  for (uint32_t t = 0; t <= duration; t += 1000) {
    const GovernorAction a = g.poll(start + t, true, network_was_ready);
    if (a == GovernorAction::RecoverInPlace) {
      ++o.recovers;
      if (o.recover_at == 0) o.recover_at = t;
    } else if (a == GovernorAction::Reboot) {
      ++o.reboots;
      if (o.reboot_at == 0) o.reboot_at = t;
    }
  }
  return o;
}
}  // namespace

TEST_CASE(governor_healthy_takes_no_action) {
  NetworkRebootGovernor g;
  for (uint32_t t = 0; t < 2 * kHour; t += 1000) {
    ASSERT_TRUE(g.poll(t, false, true) == GovernorAction::None);
  }
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_TRUE(!g.take_count_reset());
}

TEST_CASE(governor_first_episode_recovers_at_2min_reboots_at_10min) {
  NetworkRebootGovernor g;
  const Outcome o = run_isolated(g, 5000, 30 * kMin);
  ASSERT_EQ(o.recovers, 1);
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboots, 1);  // returned once even if the caller fails to restart
  ASSERT_EQ(o.reboot_at, 10 * kMin);
  ASSERT_EQ(g.reboot_count(), 1u);
}

TEST_CASE(governor_ladder_follows_persisted_count) {
  NetworkRebootGovernor g1(NetworkRebootConfig(), 1);
  const Outcome o1 = run_isolated(g1, 0, 2 * kHour);
  ASSERT_EQ(o1.reboot_at, 1 * kHour);
  ASSERT_EQ(g1.reboot_count(), 2u);

  NetworkRebootGovernor g2(NetworkRebootConfig(), 2);
  const Outcome o2 = run_isolated(g2, 0, 5 * kHour);
  ASSERT_EQ(o2.reboot_at, 4 * kHour);
  ASSERT_EQ(g2.reboot_count(), 3u);

  NetworkRebootGovernor g3(NetworkRebootConfig(), 3);
  const Outcome o3 = run_isolated(g3, 0, 25 * kHour);
  ASSERT_EQ(o3.reboot_at, 24 * kHour);
  ASSERT_EQ(g3.reboot_count(), 4u);

  NetworkRebootGovernor g9(NetworkRebootConfig(), 9);
  const Outcome o9 = run_isolated(g9, 0, 25 * kHour);
  ASSERT_EQ(o9.reboot_at, 24 * kHour);
}

TEST_CASE(governor_total_outage_has_three_reboots_in_first_24h) {
  // Simulate the device restarting each time Reboot is returned.
  uint32_t persisted = 0;
  uint32_t elapsed = 0;
  int reboots = 0;
  while (elapsed < 24 * kHour) {
    NetworkRebootGovernor g(NetworkRebootConfig(), persisted);
    const Outcome o = run_isolated(g, 0, 24 * kHour - elapsed);
    if (o.reboots == 0) break;
    ++reboots;
    elapsed += o.reboot_at;
    persisted = g.reboot_count();
  }
  ASSERT_EQ(reboots, 3);  // at 10 min, 1 h 10 min, 5 h 10 min
}

TEST_CASE(governor_recovers_once_per_episode) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 3);  // 24 h to reboot: long episode
  const Outcome o = run_isolated(g, 0, 6 * kHour);
  ASSERT_EQ(o.recovers, 1);
  // A new episode gets its own recovery.
  ASSERT_TRUE(g.poll(6 * kHour + 1000, false, true) == GovernorAction::None);
  const Outcome o2 = run_isolated(g, 6 * kHour + 2000, 5 * kMin);
  ASSERT_EQ(o2.recovers, 1);
}

TEST_CASE(governor_cold_boot_in_outage_never_reboots) {
  NetworkRebootGovernor g;  // stored count 0, network never ready
  const Outcome o = run_isolated(g, 0, 48 * kHour, false);
  ASSERT_EQ(o.recovers, 0);
  ASSERT_EQ(o.reboots, 0);
  ASSERT_EQ(g.ms_until_reboot(48 * kHour), 0u);
}

TEST_CASE(governor_rebooted_into_outage_keeps_laddering_without_network) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 1);  // we rebooted for isolation once
  const Outcome o = run_isolated(g, 0, 2 * kHour, false);
  ASSERT_EQ(o.reboots, 1);
  ASSERT_EQ(o.reboot_at, 1 * kHour);
}

TEST_CASE(governor_flapping_restarts_isolation_clock) {
  NetworkRebootGovernor g;
  uint32_t t = 0;
  int reboots = 0;
  // 9 minutes isolated, 5 seconds healthy, repeated for 3 hours: never reaches 10 min.
  for (int cycle = 0; cycle < 20; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) {
      if (g.poll(t, true, true) == GovernorAction::Reboot) ++reboots;
    }
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) {
      g.poll(t, false, true);
    }
  }
  ASSERT_EQ(reboots, 0);
}

TEST_CASE(governor_count_resets_after_10_healthy_minutes) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  for (uint32_t t = 0; t < 10 * kMin; t += 1000) {
    g.poll(t, false, true);
    ASSERT_TRUE(!g.take_count_reset());
  }
  g.poll(10 * kMin, false, true);
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_TRUE(g.take_count_reset());
  ASSERT_TRUE(!g.take_count_reset());  // reported once
}

TEST_CASE(governor_short_health_does_not_reset_count) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  uint32_t t = 0;
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) g.poll(t, false, true);
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) g.poll(t, true, true);
  }
  ASSERT_EQ(g.reboot_count(), 2u);
  ASSERT_TRUE(!g.take_count_reset());
}

TEST_CASE(governor_ms_until_reboot_counts_down) {
  NetworkRebootGovernor g;
  ASSERT_EQ(g.ms_until_reboot(0), 0u);  // not isolated
  g.poll(1000, true, true);
  ASSERT_EQ(g.ms_until_reboot(1000), 10 * kMin);
  g.poll(4 * kMin + 1000, true, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 1000), 6 * kMin);
  g.poll(4 * kMin + 2000, false, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 2000), 0u);
}

TEST_CASE(governor_survives_millis_wraparound) {
  NetworkRebootGovernor g;
  const Outcome o = run_isolated(g, 0xFFFFF000u, 30 * kMin);  // wraps after ~4 s
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboot_at, 10 * kMin);
}

#endif
