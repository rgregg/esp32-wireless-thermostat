#if defined(THERMOSTAT_RUN_TESTS)
#include <vector>

#include "test_harness.h"
#include "transport/espnow_channel_policy.h"

using thermostat::ChannelAction;
using thermostat::ChannelActionType;
using thermostat::ChannelInputs;
using thermostat::EspNowChannelConfig;
using thermostat::EspNowChannelPolicy;
using thermostat::LinkRole;

namespace {

EspNowChannelConfig cfg(LinkRole role, uint32_t interval_ms) {
  EspNowChannelConfig c;
  c.role = role;
  c.rendezvous_channel = 6;
  c.search_interval_ms = interval_ms;
  return c;
}

// Simulates a radio and an unassociated device searching for a peer that listens on
// `peer_channel` (0 = no peer anywhere). The peer never sends frames unless a test
// sets peer_heard/last_peer_rx_ms itself.
struct Sim {
  EspNowChannelPolicy policy;
  uint32_t now;
  uint8_t radio = 6;
  uint8_t peer_channel = 0;
  bool answer_probes = true;
  std::vector<uint8_t> probes;
  std::vector<uint8_t> sets;
  ChannelInputs in;

  Sim(const EspNowChannelConfig &c, uint32_t start_ms = 1000) : policy(c), now(start_ms) {}

  ChannelAction step(uint32_t dt_ms = 10) {
    now += dt_ms;
    in.now_ms = now;
    const ChannelAction a = policy.poll(in);
    if (a.type == ChannelActionType::SetChannel) {
      radio = a.channel;
      sets.push_back(a.channel);
    } else if (a.type == ChannelActionType::Probe) {
      probes.push_back(radio);
      if (answer_probes) policy.on_probe_result(a.channel, peer_channel != 0 && radio == peer_channel);
    }
    return a;
  }

  void run(uint32_t duration_ms, uint32_t dt_ms = 10) {
    const uint32_t steps = duration_ms / dt_ms;
    for (uint32_t i = 0; i < steps; ++i) step(dt_ms);
  }
};

bool only_allowed(const std::vector<uint8_t> &chs, uint8_t extra) {
  for (uint8_t c : chs) {
    if (c != 1 && c != 6 && c != 11 && c != extra) return false;
  }
  return true;
}

}  // namespace

TEST_CASE(channel_associated_takes_no_channel_actions) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = true;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
  ASSERT_EQ(s.policy.search_count(), 0u);
}

TEST_CASE(channel_unassociated_linked_stays_parked) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.peer_heard = true;
  for (int i = 0; i < 1000; ++i) {
    s.in.last_peer_rx_ms = s.now;  // heard continuously
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
  ASSERT_EQ(s.policy.search_count(), 0u);
}

TEST_CASE(channel_search_order_is_last_good_then_rendezvous_then_rest) {
  Sim s(cfg(LinkRole::Controller, 20000));
  // Establish last-good channel 11 by being associated and linked there.
  s.in.wifi_associated = true;
  s.in.wifi_channel = 11;
  s.in.mqtt_up = true;
  s.in.peer_heard = true;
  s.in.last_peer_rx_ms = s.now;
  s.radio = 11;
  s.step();
  // Lose WiFi and the peer.
  s.in.wifi_associated = false;
  s.in.mqtt_up = false;
  s.run(31000);  // link times out, one pass runs
  ASSERT_TRUE(s.probes.size() >= 3);
  ASSERT_EQ(static_cast<int>(s.probes[0]), 11);
  ASSERT_EQ(static_cast<int>(s.probes[1]), 6);
  ASSERT_EQ(static_cast<int>(s.probes[2]), 1);
  // Between passes the radio is parked on the rendezvous channel.
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 6);
}

TEST_CASE(channel_search_only_uses_1_6_11) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.run(300000, 50);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_TRUE(only_allowed(s.probes, 0));
  ASSERT_TRUE(only_allowed(s.sets, 0));
}

TEST_CASE(channel_last_good_nonstandard_channel_is_tried_first) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 3;
  s.in.mqtt_up = true;
  s.in.peer_heard = true;
  s.in.last_peer_rx_ms = s.now;
  s.radio = 3;
  s.step();
  s.in.wifi_associated = false;
  s.in.mqtt_up = false;
  s.peer_channel = 3;
  s.run(31000);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_EQ(static_cast<int>(s.probes[0]), 3);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 3);
  ASSERT_TRUE(only_allowed(s.probes, 3));
}

TEST_CASE(channel_search_runs_without_any_prior_link) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 11;
  s.run(2000);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 11);
  ASSERT_EQ(s.policy.search_count(), 1u);
}

TEST_CASE(channel_ack_holds_channel_until_hold_expires) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 1;
  s.run(2000);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
  const size_t sets_after_find = s.sets.size();
  const size_t probes_after_find = s.probes.size();
  s.run(25000);  // inside the 30 s hold, peer still silent
  ASSERT_EQ(s.sets.size(), sets_after_find);
  ASSERT_EQ(s.probes.size(), probes_after_find);
  s.run(10000);  // hold expired with no frame: searching resumes
  ASSERT_TRUE(s.probes.size() > probes_after_find);
}

TEST_CASE(channel_link_established_after_ack_stops_search) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 1;
  s.run(2000);
  s.in.peer_heard = true;
  const uint32_t count = s.policy.search_count();
  for (int i = 0; i < 6000; ++i) {
    s.in.last_peer_rx_ms = s.now;
    s.step();
  }
  ASSERT_EQ(s.policy.search_count(), count);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
}

TEST_CASE(channel_search_interval_differs_by_config) {
  Sim controller(cfg(LinkRole::Controller, 20000));
  controller.run(59000);
  ASSERT_EQ(controller.policy.search_count(), 3u);  // t=0, 20 s, 40 s

  Sim display(cfg(LinkRole::Display, 5000));
  display.run(59000);
  ASSERT_EQ(display.policy.search_count(), 12u);  // every 5 s
}

TEST_CASE(channel_probe_timeout_moves_to_next_candidate) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;  // send callback never arrives
  s.run(2000);
  ASSERT_TRUE(s.probes.size() >= 3);
  ASSERT_EQ(static_cast<int>(s.probes[0]), 6);
  ASSERT_EQ(static_cast<int>(s.probes[1]), 1);
  ASSERT_EQ(static_cast<int>(s.probes[2]), 11);
}

TEST_CASE(channel_late_ack_is_ignored) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;
  s.run(450);  // probe on 6 timed out; policy has moved on to channel 1
  ASSERT_TRUE(s.probes.size() >= 2);
  s.run(1000);  // pass finishes
  s.policy.on_probe_result(6, true);  // stale ack, no probe awaited
  s.run(100);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 6);  // parked on rendezvous
  // The stale ack must not have started a hold: the next pass still runs on schedule.
  const uint32_t count = s.policy.search_count();
  s.run(6000);
  ASSERT_TRUE(s.policy.search_count() > count);
}

TEST_CASE(channel_search_survives_millis_wraparound) {
  // Peer absent: passes must keep running on the 5 s interval across the wrap.
  Sim s(cfg(LinkRole::Display, 5000), 0xFFFFFFFFu - 100u);
  s.run(20000);
  ASSERT_EQ(s.policy.search_count(), 4u);  // t=10 ms, 5 s, 10 s, 15 s

  // A find made after the wrap holds for the full 30 s.
  Sim f(cfg(LinkRole::Display, 5000), 0xFFFFFFFFu - 100u);
  f.peer_channel = 11;
  f.run(500);  // first pass wraps and finds the peer
  ASSERT_EQ(static_cast<int>(f.policy.current_channel()), 11);
  const size_t sets = f.sets.size();
  const size_t probes = f.probes.size();
  f.run(29000);
  ASSERT_EQ(f.sets.size(), sets);
  ASSERT_EQ(f.probes.size(), probes);
  f.run(2000);  // hold expired
  ASSERT_TRUE(f.probes.size() > probes);
}

TEST_CASE(channel_probe_result_for_other_channel_is_ignored) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;
  // Step until the probe on channel 1 (second candidate) is outstanding.
  while (s.probes.size() < 2) s.step();
  ASSERT_EQ(static_cast<int>(s.probes[1]), 1);
  s.policy.on_probe_result(6, true);  // late ack for the previous candidate
  s.run(2000);
  ASSERT_TRUE(s.probes.size() >= 3);
  ASSERT_EQ(static_cast<int>(s.probes[2]), 11);  // did not hold on channel 1
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 6);
}

TEST_CASE(channel_matching_nack_advances_immediately) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;
  while (s.probes.empty()) s.step();
  s.policy.on_probe_result(s.radio, false);
  const ChannelAction next = s.step();  // 10 ms later, far below the 200 ms timeout
  ASSERT_TRUE(next.type == ChannelActionType::None);
  const ChannelAction after = s.step();
  ASSERT_TRUE(after.type == ChannelActionType::SetChannel);
  ASSERT_EQ(static_cast<int>(after.channel), 1);
}

TEST_CASE(channel_invalid_rendezvous_falls_back_to_6) {
  EspNowChannelConfig c = cfg(LinkRole::Controller, 20000);
  c.rendezvous_channel = 0;
  Sim s(c);
  s.run(2000);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_EQ(static_cast<int>(s.probes[0]), 6);
}

TEST_CASE(channel_display_suspends_wifi_when_broker_down_and_peer_unlinked) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  bool suspended = false;
  uint32_t suspended_after = 0;
  const uint32_t start = s.now;
  for (int i = 0; i < 7000 && !suspended; ++i) {
    if (s.step().type == ChannelActionType::SuspendWifi) {
      suspended = true;
      suspended_after = s.now - start;
    }
  }
  ASSERT_TRUE(suspended);
  ASSERT_TRUE(suspended_after >= 60000u && suspended_after < 61000u);
  ASSERT_TRUE(s.policy.wifi_suspended());
}

TEST_CASE(channel_controller_never_suspends_wifi) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  for (int i = 0; i < 20000; ++i) {
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

TEST_CASE(channel_display_stays_on_wifi_when_peer_linked) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  s.in.peer_heard = true;
  for (int i = 0; i < 20000; ++i) {
    s.in.last_peer_rx_ms = s.now;
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

TEST_CASE(channel_display_resumes_wifi_then_waits_before_suspending_again) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  while (s.step().type != ChannelActionType::SuspendWifi) {
  }
  const uint32_t suspended_at = s.now;
  s.in.wifi_associated = false;  // caller disconnected
  bool resumed = false;
  while (!resumed) {
    resumed = s.step().type == ChannelActionType::ResumeWifi;
  }
  ASSERT_TRUE(s.now - suspended_at >= 300000u && s.now - suspended_at < 301000u);
  ASSERT_TRUE(!s.policy.wifi_suspended());
  // WiFi comes back, broker still down: no new suspend inside the 60 s window.
  s.in.wifi_associated = true;
  const uint32_t resumed_at = s.now;
  bool suspended_again = false;
  while (!suspended_again) {
    suspended_again = s.step().type == ChannelActionType::SuspendWifi;
  }
  ASSERT_TRUE(s.now - resumed_at >= 60000u && s.now - resumed_at < 61000u);
}

TEST_CASE(channel_display_does_not_suspend_when_mqtt_returns) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  s.run(50000);
  s.in.mqtt_up = true;
  s.run(5000);
  s.in.mqtt_up = false;  // down again: the 60 s clock restarts
  for (int i = 0; i < 5000; ++i) {  // 50 s
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

TEST_CASE(channel_display_suspend_clock_restarts_on_association) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = false;
  s.in.mqtt_up = false;
  s.run(120000);
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  const uint32_t associated_at = s.now;
  bool suspended = false;
  while (!suspended && s.now - associated_at < 70000u) {
    suspended = s.step().type == ChannelActionType::SuspendWifi;
  }
  ASSERT_TRUE(suspended);
  ASSERT_TRUE(s.now - associated_at >= 60000u && s.now - associated_at < 61000u);
}

#endif
