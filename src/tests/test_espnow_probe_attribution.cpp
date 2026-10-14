#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"

#include <string.h>

#include "controller/transport/espnow_controller_transport.h"
#include "thermostat/transport/espnow_thermostat_transport.h"
#include "transport/espnow_probe_tracker.h"

// Host builds treat every esp_now_send as accepted, so each send issued by a transport
// takes the next sequence number and the tests deliver the matching send callbacks by
// calling the static callback entry point directly, in send order (as ESP-NOW does).

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

#endif
