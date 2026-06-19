#if defined(THERMOSTAT_RUN_TESTS)

#include "thermostat/thermostat_app.h"
#include "test_harness.h"

namespace {
class FakeThermostatTransport : public thermostat::IThermostatTransport {
 public:
  void publish_command_word(uint32_t) override {}
  void publish_controller_ack(uint16_t) override {}
  void publish_indoor_temperature_c(float) override {}
  void publish_indoor_humidity(float) override {}
};

thermostat::ThermostatControllerTelemetry tele(uint16_t seq,
                                               FurnaceStateCode state) {
  thermostat::ThermostatControllerTelemetry t;
  t.seq = seq;
  t.state = state;
  return t;
}
}  // namespace

// Without a reconnect gap, an older/lower seq is still rejected (dedup intact).
TEST_CASE(thermostat_telemetry_dedup_rejects_older_seq) {
  FakeThermostatTransport tx;
  thermostat::ThermostatAppConfig cfg;
  cfg.controller_reconnect_resync_ms = 30000;
  thermostat::ThermostatApp app(tx, cfg);

  app.on_controller_heartbeat(1000);
  app.on_controller_telemetry(1000, tele(5, FurnaceStateCode::HeatOn));
  ASSERT_TRUE(app.controller_state() == FurnaceStateCode::HeatOn);

  app.on_controller_heartbeat(2000);  // small gap -> no resync
  app.on_controller_telemetry(2000, tele(2, FurnaceStateCode::Idle));
  ASSERT_TRUE(app.controller_state() == FurnaceStateCode::HeatOn);  // unchanged
}

// After a reconnect gap (controller restarted with a reset seq), a lower seq IS accepted.
TEST_CASE(thermostat_telemetry_resyncs_after_reconnect_gap) {
  FakeThermostatTransport tx;
  thermostat::ThermostatAppConfig cfg;
  cfg.controller_reconnect_resync_ms = 30000;
  thermostat::ThermostatApp app(tx, cfg);

  app.on_controller_heartbeat(1000);
  app.on_controller_telemetry(1000, tele(5, FurnaceStateCode::HeatOn));
  ASSERT_TRUE(app.controller_state() == FurnaceStateCode::HeatOn);

  // Silent >= resync window, then heartbeats again -> drop seq tracking.
  app.on_controller_heartbeat(31000);
  app.on_controller_telemetry(31000, tele(1, FurnaceStateCode::Idle));
  ASSERT_TRUE(app.controller_state() == FurnaceStateCode::Idle);  // resynced
}

// Resync disabled (0) -> a lower seq after a long gap is still rejected.
TEST_CASE(thermostat_telemetry_resync_disabled_keeps_dedup) {
  FakeThermostatTransport tx;
  thermostat::ThermostatAppConfig cfg;
  cfg.controller_reconnect_resync_ms = 0;  // disabled
  thermostat::ThermostatApp app(tx, cfg);

  app.on_controller_heartbeat(1000);
  app.on_controller_telemetry(1000, tele(5, FurnaceStateCode::HeatOn));
  app.on_controller_heartbeat(1000000);  // huge gap, but resync disabled
  app.on_controller_telemetry(1000000, tele(1, FurnaceStateCode::Idle));
  ASSERT_TRUE(app.controller_state() == FurnaceStateCode::HeatOn);  // unchanged
}

#endif  // THERMOSTAT_RUN_TESTS
