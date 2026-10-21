#if defined(THERMOSTAT_RUN_TESTS)
#include <vector>

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
    const GovernorAction a = g.poll(start + t, true, false, network_was_ready);
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
    ASSERT_TRUE(g.poll(t, false, true, true) == GovernorAction::None);
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

TEST_CASE(governor_isolation_recovers_every_30_min) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 3);  // 24 h to reboot: long episode
  std::vector<uint32_t> at;
  for (uint32_t t = 0; t <= 6 * kHour; t += 1000) {
    if (g.poll(t, true, false, true) == GovernorAction::RecoverInPlace) at.push_back(t);
  }
  ASSERT_EQ(at.size(), 12u);  // 2, 32, 62 ... 332 min
  for (size_t i = 0; i < at.size(); ++i) {
    ASSERT_EQ(at[i], 2 * kMin + static_cast<uint32_t>(i) * 30 * kMin);
  }
  // A new episode starts its own schedule.
  ASSERT_TRUE(g.poll(6 * kHour + 1000, false, true, true) == GovernorAction::None);
  const Outcome o2 = run_isolated(g, 6 * kHour + 2000, 5 * kMin);
  ASSERT_EQ(o2.recovers, 1);
  ASSERT_EQ(o2.recover_at, 2 * kMin);
}

TEST_CASE(governor_high_count_recovers_between_daily_reboots) {
  // At net_rb_cnt >= 3 a wedged stack must not wait 24 h for its next recovery.
  uint32_t persisted = 3;
  for (int boot = 0; boot < 2; ++boot) {
    NetworkRebootGovernor g(NetworkRebootConfig(), persisted);
    const Outcome o = run_isolated(g, 0, 25 * kHour);
    ASSERT_EQ(o.reboots, 1);
    ASSERT_EQ(o.reboot_at, 24 * kHour);
    ASSERT_EQ(o.recovers, 48);  // 2, 32 ... 1412 min; none after the reboot was issued
    persisted = g.reboot_count();
  }
  ASSERT_EQ(persisted, 5u);
}

TEST_CASE(governor_reboot_due_with_recovery_wins) {
  NetworkRebootConfig cfg;
  cfg.first_reboot_after_ms = 32 * kMin;  // coincides with the second recovery
  NetworkRebootGovernor g(cfg, 0);
  ASSERT_TRUE(g.poll(0, true, false, true) == GovernorAction::None);
  ASSERT_TRUE(g.poll(2 * kMin, true, false, true) == GovernorAction::RecoverInPlace);
  ASSERT_TRUE(g.poll(32 * kMin, true, false, true) == GovernorAction::Reboot);
  // The recovery due at the same poll is dropped, not delivered afterwards.
  ASSERT_TRUE(g.poll(32 * kMin + 1000, true, false, true) == GovernorAction::None);
}

TEST_CASE(governor_recovers_at_2min_when_count_set_and_network_never_ready) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 1);
  const Outcome o = run_isolated(g, 0, 5 * kMin, false);
  ASSERT_EQ(o.recovers, 1);
  ASSERT_EQ(o.recover_at, 2 * kMin);
}

TEST_CASE(governor_clock_starts_when_network_becomes_ready) {
  NetworkRebootGovernor g;  // count 0
  for (uint32_t t = 0; t < 30 * kMin; t += 1000) {
    ASSERT_TRUE(g.poll(t, true, false, false) == GovernorAction::None);
  }
  ASSERT_EQ(g.ms_until_reboot(30 * kMin), 0u);
  const Outcome o = run_isolated(g, 30 * kMin, 15 * kMin, true);
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboot_at, 10 * kMin);
}

TEST_CASE(governor_ms_until_reboot_is_zero_once_reboot_issued) {
  NetworkRebootGovernor g;
  run_isolated(g, 0, 10 * kMin);  // Reboot returned at 10 min
  ASSERT_EQ(g.reboot_count(), 1u);
  ASSERT_EQ(g.ms_until_reboot(10 * kMin + 1000), 0u);
}

TEST_CASE(governor_count_kept_after_9_healthy_minutes_sets_threshold) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  for (uint32_t t = 0; t < 9 * kMin; t += 1000) g.poll(t, false, true, true);
  ASSERT_EQ(g.reboot_count(), 2u);
  const Outcome o = run_isolated(g, 9 * kMin, 5 * kHour);
  ASSERT_EQ(o.reboot_at, 4 * kHour);  // count 2's threshold, not 10 min
}

// --- MQTT down while the ESP-NOW peer is linked (not isolated) ---

TEST_CASE(governor_mqtt_down_but_linked_recovers_hourly_never_reboots) {
  NetworkRebootGovernor g;
  std::vector<uint32_t> at;
  for (uint32_t t = 0; t <= 4 * kHour; t += 1000) {
    const GovernorAction a = g.poll(t, false, false, true);
    ASSERT_TRUE(a != GovernorAction::Reboot);
    if (a == GovernorAction::RecoverInPlace) at.push_back(t);
  }
  ASSERT_EQ(at.size(), 4u);
  for (size_t i = 0; i < at.size(); ++i) {
    ASSERT_EQ(at[i], static_cast<uint32_t>(i + 1) * kHour);
  }
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_EQ(g.ms_until_reboot(4 * kHour), 0u);
}

TEST_CASE(governor_mqtt_up_restarts_self_heal_clock) {
  NetworkRebootGovernor g;
  uint32_t t = 0;
  for (; t < 59 * kMin; t += 1000) {
    ASSERT_TRUE(g.poll(t, false, false, true) == GovernorAction::None);
  }
  g.poll(t, false, true, true);  // MQTT back for a moment
  t += 1000;
  const uint32_t down_again = t;
  uint32_t recovered_at = 0;
  for (; t <= down_again + 2 * kHour && recovered_at == 0; t += 1000) {
    if (g.poll(t, false, false, true) == GovernorAction::RecoverInPlace) recovered_at = t;
  }
  ASSERT_EQ(recovered_at - down_again, 1 * kHour);
}

TEST_CASE(governor_self_heal_waits_for_network_ready_when_count_zero) {
  NetworkRebootGovernor g;
  for (uint32_t t = 0; t <= 3 * kHour; t += 1000) {
    ASSERT_TRUE(g.poll(t, false, false, false) == GovernorAction::None);
  }
  // (With a stored count the gate does not apply, but 10 non-isolated minutes reset the
  // count to 0 and close the gate again: the recorded "count reset after ESP-NOW-only
  // health" limitation, deliberately not changed here.)
}

TEST_CASE(governor_self_heal_due_while_isolated_is_left_to_the_ladder) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 3);  // no reboot within the test
  uint32_t t = 0;
  int heals = 0;
  for (; t < 50 * kMin; t += 1000) {
    if (g.poll(t, false, false, true) == GovernorAction::RecoverInPlace) ++heals;
  }
  int ladder = 0;
  for (; t < 65 * kMin; t += 1000) {  // isolated across the 60 min mark
    if (g.poll(t, true, false, true) == GovernorAction::RecoverInPlace) ++ladder;
  }
  for (; t < 119 * kMin; t += 1000) {  // linked again, MQTT still down
    if (g.poll(t, false, false, true) == GovernorAction::RecoverInPlace) ++heals;
  }
  ASSERT_EQ(ladder, 1);  // at 2 min of isolation
  ASSERT_EQ(heals, 0);   // the 60 min one was skipped, not delivered late
  ASSERT_TRUE(g.poll(120 * kMin, false, false, true) == GovernorAction::RecoverInPlace);
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
  int recovers = 0;
  // 9 minutes isolated, 5 seconds healthy, repeated for 3 hours: never reaches 10 min.
  for (int cycle = 0; cycle < 20; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) {
      const GovernorAction a = g.poll(t, true, false, true);
      if (a == GovernorAction::Reboot) ++reboots;
      if (a == GovernorAction::RecoverInPlace) ++recovers;
    }
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) {
      g.poll(t, false, true, true);
    }
  }
  ASSERT_EQ(reboots, 0);
  ASSERT_EQ(recovers, 20);  // one per 9-minute episode: the 30-min repeat never comes
}

TEST_CASE(governor_count_resets_after_10_healthy_minutes) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  for (uint32_t t = 0; t < 10 * kMin; t += 1000) {
    g.poll(t, false, true, true);
    ASSERT_TRUE(!g.take_count_reset());
  }
  g.poll(10 * kMin, false, true, true);
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_TRUE(g.take_count_reset());
  ASSERT_TRUE(!g.take_count_reset());  // reported once
}

TEST_CASE(governor_short_health_does_not_reset_count) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  uint32_t t = 0;
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) g.poll(t, false, true, true);
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) g.poll(t, true, false, true);
  }
  ASSERT_EQ(g.reboot_count(), 2u);
  ASSERT_TRUE(!g.take_count_reset());
}

TEST_CASE(governor_ms_until_reboot_counts_down) {
  NetworkRebootGovernor g;
  ASSERT_EQ(g.ms_until_reboot(0), 0u);  // not isolated
  g.poll(1000, true, false, true);
  ASSERT_EQ(g.ms_until_reboot(1000), 10 * kMin);
  g.poll(4 * kMin + 1000, true, false, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 1000), 6 * kMin);
  g.poll(4 * kMin + 2000, false, true, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 2000), 0u);
}

TEST_CASE(governor_survives_millis_wraparound) {
  NetworkRebootGovernor g;
  const Outcome o = run_isolated(g, 0xFFFFF000u, 30 * kMin);  // wraps after ~4 s
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboot_at, 10 * kMin);
}

TEST_CASE(governor_mqtt_down_recovery_survives_50_days_and_wraparound) {
  NetworkRebootGovernor g;
  int recovers = 0;
  const uint32_t start = 0xFFF00000u;
  uint32_t t = 0;
  for (uint32_t i = 0; i <= 50u * 24u * 60u; ++i, t += kMin) {  // one poll a minute
    const GovernorAction a = g.poll(start + t, false, false, true);
    ASSERT_TRUE(a != GovernorAction::Reboot);
    if (a == GovernorAction::RecoverInPlace) ++recovers;
  }
  ASSERT_EQ(recovers, 50 * 24);
}

#endif
