#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"

#include <string.h>

#include "controller/transport/espnow_controller_transport.h"
#include "thermostat/transport/espnow_thermostat_transport.h"
#include "transport/espnow_probe_tracker.h"

// Host builds treat every esp_now_send as accepted, so each send issued by a transport
// takes the next sequence number and the tests deliver the matching send callbacks by
// calling the static callback entry point directly, in send order (as ESP-NOW does).
// Each transport is a local of its test; its destructor releases the static callback
// instance, so no later test can reach a dangling pointer through on_send_static().

using thermostat::EspNowControllerConfig;
using thermostat::EspNowControllerTransport;
using thermostat::EspNowThermostatConfig;
using thermostat::EspNowThermostatTransport;
using thermostat::ProbeStatus;

namespace {

constexpr int kOk = 0;    // ESP_NOW_SEND_SUCCESS
constexpr int kFail = 1;  // ESP_NOW_SEND_FAIL

const uint8_t kPeerA[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
const uint8_t kPeerB[6] = {0x24, 0x6F, 0x28, 0x0A, 0x0B, 0x0C};
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

EspNowControllerConfig controller_config(int unicast_peers) {
  EspNowControllerConfig cfg;
  if (unicast_peers >= 1) memcpy(cfg.peer_macs[cfg.peer_count++], kPeerA, 6);
  if (unicast_peers >= 2) memcpy(cfg.peer_macs[cfg.peer_count++], kPeerB, 6);
  return cfg;
}

void ctrl_cb(int status) { EspNowControllerTransport::on_send_static(nullptr, status); }
void disp_cb(int status) { EspNowThermostatTransport::on_send_static(nullptr, status); }

}  // namespace

TEST_CASE(espnow_ctrl_probe_own_ack_reports_acked) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  ASSERT_TRUE(t.has_unicast_peer());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  ctrl_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
  t.clear_probe();
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);
}

TEST_CASE(espnow_ctrl_probe_own_failure_reports_failed) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kFail);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Failed);
}

TEST_CASE(espnow_ctrl_ack_for_send_before_probe_is_not_attributed) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  t.publish_time(1800000000UL);  // routine send issued before the probe
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kOk);  // late ack for the routine send
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  ctrl_cb(kFail);  // the probe's own callback
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Failed);
}

TEST_CASE(espnow_ctrl_two_peer_probe_acked_if_either_frame_acked) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(2)));
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kFail);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);  // second frame outstanding
  ctrl_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);

  t.clear_probe();
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);  // still waits for both
  ctrl_cb(kFail);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_ctrl_restart_clears_pending_probe_and_resyncs) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  t.publish_time(1800000000UL);
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  ASSERT_TRUE(t.restart());  // pending callbacks are dropped by esp_now_deinit
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kOk);  // first callback after restart belongs to the new probe
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_ctrl_probe_needs_unicast_peer) {
  EspNowControllerTransport t;
  EspNowControllerConfig cfg;
  memcpy(cfg.peer_macs[cfg.peer_count++], kBroadcast, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(!t.has_unicast_peer());
  ASSERT_TRUE(!t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);

  EspNowControllerTransport not_started;
  ASSERT_TRUE(!not_started.send_probe());
}

TEST_CASE(espnow_disp_probe_own_ack_reports_acked) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(t.has_unicast_peer());
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  disp_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
  t.clear_probe();
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);
}

TEST_CASE(espnow_disp_ack_for_send_before_probe_is_not_attributed) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  t.publish_indoor_temperature_c(21.0f);  // routine send issued before the probe
  ASSERT_TRUE(t.send_probe());
  disp_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  disp_cb(kFail);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Failed);
}

TEST_CASE(espnow_disp_restart_clears_pending_probe_and_resyncs) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  t.publish_indoor_temperature_c(21.0f);
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.restart());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::None);
  ASSERT_TRUE(t.send_probe());
  disp_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_disp_probe_needs_unicast_peer) {
  EspNowThermostatTransport broadcast;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kBroadcast, 6);
  ASSERT_TRUE(broadcast.begin(cfg));
  ASSERT_TRUE(!broadcast.has_unicast_peer());
  ASSERT_TRUE(!broadcast.send_probe());

  EspNowThermostatTransport zero;
  ASSERT_TRUE(zero.begin(EspNowThermostatConfig()));
  ASSERT_TRUE(!zero.has_unicast_peer());
  ASSERT_TRUE(!zero.send_probe());
}

TEST_CASE(espnow_ctrl_stray_callback_is_clamped_at_probe_start) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  ctrl_cb(kOk);  // stray callback with no send outstanding
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);  // not completed by the stray
  ctrl_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_disp_stray_callback_is_clamped_at_probe_start) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  disp_cb(kOk);
  disp_cb(kFail);
  ASSERT_TRUE(t.send_probe());
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Pending);
  disp_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

// --- Lost send callback guard (EspNowProbeTracker::check_lost_callbacks) ---

TEST_CASE(espnow_probe_tracker_resyncs_after_lost_callback) {
  thermostat::EspNowProbeTracker tr;
  tr.on_send_accepted();  // its callback never arrives
  tr.check_lost_callbacks(10000);
  tr.check_lost_callbacks(11000);  // behind for exactly 1 s: not yet
  tr.check_lost_callbacks(11001);  // more than 1 s: resync
  tr.begin_probe();
  tr.on_send_accepted();
  ASSERT_TRUE(tr.end_probe());
  tr.on_send_complete(true);  // the probe's own callback
  ASSERT_TRUE(tr.status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_probe_tracker_without_guard_lost_callback_blocks_probe) {
  // Documents why the guard exists: the probe's callback is miscounted as the lost one.
  thermostat::EspNowProbeTracker tr;
  tr.on_send_accepted();
  tr.begin_probe();
  tr.on_send_accepted();
  ASSERT_TRUE(tr.end_probe());
  tr.on_send_complete(true);
  ASSERT_TRUE(tr.status() == ProbeStatus::Pending);
}

TEST_CASE(espnow_probe_tracker_no_resync_while_probe_outstanding) {
  thermostat::EspNowProbeTracker tr;
  tr.begin_probe();
  tr.on_send_accepted();
  ASSERT_TRUE(tr.end_probe());
  for (uint32_t t = 0; t <= 3000; t += 100) tr.check_lost_callbacks(t);
  ASSERT_TRUE(tr.status() == ProbeStatus::Pending);  // probe kept, not resynced away
  tr.on_send_complete(true);
  ASSERT_TRUE(tr.status() == ProbeStatus::Acked);
}

TEST_CASE(espnow_probe_tracker_lag_clock_restarts_when_caught_up) {
  thermostat::EspNowProbeTracker tr;
  tr.on_send_accepted();
  tr.check_lost_callbacks(0);
  tr.on_send_complete(true);       // arrived: caught up
  tr.check_lost_callbacks(500);
  tr.on_send_accepted();           // a new send, behind again from 900 ms
  tr.check_lost_callbacks(900);
  tr.check_lost_callbacks(1500);   // 600 ms behind (1.5 s since the first lag): no resync
  tr.begin_probe();
  tr.on_send_accepted();
  ASSERT_TRUE(tr.end_probe());
  // The late callback is still attributed to its own send, so it cannot ack the probe
  // (after a wrong resync it would).
  tr.on_send_complete(true);
  tr.on_send_complete(false);      // the probe's own callback
  ASSERT_TRUE(tr.status() == ProbeStatus::Failed);
}

// --- Command frame delivery tracking ---

using thermostat::CommandDelivery;

TEST_CASE(espnow_disp_command_delivery_reported_once) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(t.reports_command_delivery());
  t.publish_indoor_temperature_c(21.0f);  // routine send ahead of the command
  ASSERT_TRUE(t.publish_command_word(0x1234u));
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Pending);
  disp_cb(kFail);  // the temperature frame's callback: not the command's
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Pending);
  disp_cb(kOk);
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Delivered);
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::None);
}

TEST_CASE(espnow_disp_command_delivery_failure_reported) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(t.publish_command_word(0x1234u));
  disp_cb(kFail);
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Failed);
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::None);
}

TEST_CASE(espnow_disp_newer_command_replaces_tracked_frame) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(t.publish_command_word(0x1u));
  ASSERT_TRUE(t.publish_command_word(0x2u));
  disp_cb(kFail);  // first command's callback: replaced, so not reported
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Pending);
  disp_cb(kOk);
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Delivered);
}

TEST_CASE(espnow_disp_restart_fails_pending_command) {
  EspNowThermostatTransport t;
  EspNowThermostatConfig cfg;
  memcpy(cfg.peer_mac, kPeerA, 6);
  ASSERT_TRUE(t.begin(cfg));
  ASSERT_TRUE(t.publish_command_word(0x1u));
  ASSERT_TRUE(t.restart());  // its callback may be lost
  ASSERT_TRUE(t.take_command_delivery() == CommandDelivery::Failed);
}

TEST_CASE(espnow_probe_tracker_tracked_frame_states) {
  thermostat::EspNowProbeTracker tr;
  using T = thermostat::EspNowProbeTracker;
  tr.begin_tracked();
  tr.end_tracked(false);  // throttled: never attempted, not tracked
  ASSERT_EQ(tr.take_tracked(), T::kTrackNone);
  tr.begin_tracked();
  tr.end_tracked(true);  // attempted but esp_now_send rejected it: no callback coming
  ASSERT_EQ(tr.take_tracked(), T::kTrackFailed);
  tr.begin_tracked();
  tr.on_send_accepted();
  tr.on_send_complete(true);  // callback before end_tracked(): still attributed
  tr.end_tracked(true);
  ASSERT_EQ(tr.take_tracked(), T::kTrackDelivered);
}

TEST_CASE(espnow_ctrl_lost_callback_guard_runs_via_transport) {
  EspNowControllerTransport t;
  ASSERT_TRUE(t.begin(controller_config(1)));
  t.publish_time(1800000000UL);  // callback lost
  t.check_lost_send_callbacks(1000);
  t.check_lost_send_callbacks(2001);
  ASSERT_TRUE(t.send_probe());
  ctrl_cb(kOk);
  ASSERT_TRUE(t.probe_status() == ProbeStatus::Acked);
}

#endif
