#if defined(THERMOSTAT_RUN_TESTS)
#include "command_builder.h"
#include "thermostat/thermostat_app.h"
#include "transport/espnow_packets.h"
#include "test_harness.h"

namespace {
class FakeThermostatTransport : public thermostat::IThermostatTransport {
 public:
  void publish_command_word(uint32_t packed_word) override {
    last_cmd = packed_word;
    ++cmd_count;
  }
  void publish_controller_ack(uint16_t seq) override {
    last_ack_seq = seq;
    ++ack_count;
  }
  void publish_indoor_temperature_c(float temp_c) override { last_temp = temp_c; }
  void publish_indoor_humidity(float humidity_pct) override { last_hum = humidity_pct; }

  uint32_t last_cmd = 0;
  int cmd_count = 0;
  uint16_t last_ack_seq = 0;
  int ack_count = 0;
  float last_temp = 0.0f;
  float last_hum = 0.0f;
};
}  // namespace

TEST_CASE(thermostat_app_command_and_debounce) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  app.set_local_setpoint_c(22.5f, 1000);
  ASSERT_TRUE(tx.cmd_count == 1);
  ASSERT_TRUE(app.has_last_packed_command());
  ASSERT_EQ(app.last_packed_command(), tx.last_cmd);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(1));
  {
    CommandWord decoded = espnow_cmd::decode(tx.last_cmd);
    ASSERT_EQ(static_cast<int>(decoded.mode), static_cast<int>(FurnaceMode::Off));
    ASSERT_EQ(decoded.setpoint_decic, static_cast<uint16_t>(225));
    ASSERT_TRUE(decoded.preserve_mode);
    ASSERT_TRUE(decoded.preserve_fan);
    ASSERT_TRUE(!decoded.preserve_setpoint);
  }

  thermostat::ThermostatControllerTelemetry telem;
  telem.seq = 1;
  telem.mode_code = 2;
  telem.fan_code = 1;
  telem.setpoint_c = 18.0f;

  app.on_controller_telemetry(2000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 22.5f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 1);
  ASSERT_EQ(tx.ack_count, 1);

  telem.seq = 2;
  app.on_controller_telemetry(7000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 18.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 2);
  ASSERT_EQ(tx.ack_count, 2);

  app.request_sync(7100);
  ASSERT_TRUE(app.last_packed_command() == tx.last_cmd);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(2));
  {
    CommandWord decoded = espnow_cmd::decode(tx.last_cmd);
    ASSERT_EQ(static_cast<int>(decoded.mode), static_cast<int>(FurnaceMode::Cool));
    ASSERT_EQ(static_cast<int>(decoded.fan), static_cast<int>(FanMode::AlwaysOn));
    ASSERT_EQ(decoded.setpoint_decic, static_cast<uint16_t>(180));
    ASSERT_TRUE(decoded.sync_request);
  }

  app.publish_indoor_temperature_c(21.2f);
  app.publish_indoor_humidity(44.0f);
  ASSERT_NEAR(tx.last_temp, 21.2f, 0.01f);
  ASSERT_NEAR(tx.last_hum, 44.0f, 0.01f);
}

TEST_CASE(thermostat_app_ignores_duplicate_and_stale_controller_seq) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  thermostat::ThermostatControllerTelemetry telem;
  telem.seq = 10;
  telem.mode_code = 1;
  telem.setpoint_c = 20.0f;
  app.on_controller_telemetry(1000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);

  telem.seq = 10;  // duplicate
  telem.setpoint_c = 25.0f;
  app.on_controller_telemetry(2000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);

  telem.seq = 9;  // stale
  telem.setpoint_c = 15.0f;
  app.on_controller_telemetry(3000, telem);
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
  ASSERT_EQ(tx.last_ack_seq, 10);
}

TEST_CASE(thermostat_app_reset_local_command_sequence_restarts_at_one) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  app.set_local_mode(FurnaceMode::Heat, 1000);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(1));

  app.set_local_mode(FurnaceMode::Cool, 2000);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(2));

  app.reset_local_command_sequence();
  ASSERT_TRUE(!app.has_last_packed_command());

  app.set_local_mode(FurnaceMode::Off, 3000);
  ASSERT_EQ(app.last_command_seq(), static_cast<uint16_t>(1));
}

TEST_CASE(thermostat_app_controller_state_update_sets_fields) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  // Initially no telemetry
  ASSERT_TRUE(!app.has_controller_telemetry());
  ASSERT_TRUE(!app.controller_connected(1000, 30000));

  // Apply MQTT-sourced controller state
  app.on_controller_state_update(
      1000, FurnaceStateCode::HeatOn, true,
      FurnaceMode::Heat, FanMode::AlwaysOn, 23.5f, 7200, false);

  ASSERT_TRUE(app.has_controller_telemetry());
  ASSERT_TRUE(app.controller_connected(1000, 30000));
  ASSERT_EQ(static_cast<int>(app.controller_state()),
            static_cast<int>(FurnaceStateCode::HeatOn));
  ASSERT_TRUE(app.controller_lockout());
  ASSERT_NEAR(app.controller_setpoint_c(), 23.5f, 0.01f);
  ASSERT_EQ(app.controller_filter_runtime_seconds(), 7200u);

  // Mode/fan/setpoint should also sync (no recent local interaction)
  ASSERT_EQ(static_cast<int>(app.local_mode()),
            static_cast<int>(FurnaceMode::Heat));
  ASSERT_EQ(static_cast<int>(app.local_fan_mode()),
            static_cast<int>(FanMode::AlwaysOn));
  ASSERT_NEAR(app.local_setpoint_c(), 23.5f, 0.01f);

  // No ESP-NOW ACK should be sent
  ASSERT_EQ(tx.ack_count, 0);
}

TEST_CASE(thermostat_app_controller_state_update_respects_debounce) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  // Local interaction at t=1000
  app.set_local_mode(FurnaceMode::Cool, 1000);

  // Controller state update within debounce window (default 5000ms)
  app.on_controller_state_update(
      3000, FurnaceStateCode::HeatOn, false,
      FurnaceMode::Heat, FanMode::Automatic, 25.0f, 0, false);

  // Controller fields should update
  ASSERT_TRUE(app.has_controller_telemetry());
  ASSERT_EQ(static_cast<int>(app.controller_state()),
            static_cast<int>(FurnaceStateCode::HeatOn));

  // But local mode/fan/setpoint should NOT be overwritten (within debounce)
  ASSERT_EQ(static_cast<int>(app.local_mode()),
            static_cast<int>(FurnaceMode::Cool));

  // After debounce expires, should sync
  app.on_controller_state_update(
      7000, FurnaceStateCode::Idle, false,
      FurnaceMode::Off, FanMode::Circulate, 20.0f, 3600, false);

  ASSERT_EQ(static_cast<int>(app.local_mode()),
            static_cast<int>(FurnaceMode::Off));
  ASSERT_EQ(static_cast<int>(app.local_fan_mode()),
            static_cast<int>(FanMode::Circulate));
  ASSERT_NEAR(app.local_setpoint_c(), 20.0f, 0.01f);
}

TEST_CASE(thermostat_app_per_mode_setpoints_independent) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  // Switch to heat at 21.0°C
  app.set_local_mode(FurnaceMode::Heat, 1000);
  app.set_local_setpoint_c(21.0f, 1100);
  ASSERT_NEAR(app.local_heat_setpoint_c(), 21.0f, 0.01f);
  ASSERT_NEAR(app.local_setpoint_c(), 21.0f, 0.01f);

  // Switch to cool at 23.5°C — heat bin must be preserved
  app.set_local_mode(FurnaceMode::Cool, 2000);
  // Switching mode itself sends a command using the current cool bin (default 24.0)
  ASSERT_NEAR(app.local_setpoint_c(), 24.0f, 0.01f);
  app.set_local_setpoint_c(23.5f, 2100);
  ASSERT_NEAR(app.local_cool_setpoint_c(), 23.5f, 0.01f);
  ASSERT_NEAR(app.local_heat_setpoint_c(), 21.0f, 0.01f);

  // Back to heat — cool bin preserved, heat bin restored
  app.set_local_mode(FurnaceMode::Heat, 3000);
  ASSERT_NEAR(app.local_setpoint_c(), 21.0f, 0.01f);
  ASSERT_NEAR(app.local_cool_setpoint_c(), 23.5f, 0.01f);

  // Sent command after final mode switch carries the heat bin (21.0), not 23.5,
  // and is flagged as a mode-only change so the controller preserves its bins.
  {
    CommandWord decoded = espnow_cmd::decode(tx.last_cmd);
    ASSERT_EQ(static_cast<int>(decoded.mode), static_cast<int>(FurnaceMode::Heat));
    ASSERT_EQ(decoded.setpoint_decic, static_cast<uint16_t>(210));
    ASSERT_TRUE(decoded.preserve_setpoint);
    ASSERT_TRUE(decoded.preserve_fan);
  }
}

TEST_CASE(thermostat_app_telemetry_routes_setpoint_into_mode_bin) {
  FakeThermostatTransport tx;
  thermostat::ThermostatApp app(tx);

  // Heat-mode telemetry → heat bin updated, cool bin untouched
  thermostat::ThermostatControllerTelemetry t;
  t.seq = 1;
  t.mode_code = 1;  // heat
  t.setpoint_c = 21.5f;
  app.on_controller_telemetry(1000, t);
  ASSERT_NEAR(app.local_heat_setpoint_c(), 21.5f, 0.01f);
  ASSERT_NEAR(app.local_cool_setpoint_c(), 24.0f, 0.01f);

  // Cool-mode telemetry → cool bin updated, heat bin preserved
  t.seq = 2;
  t.mode_code = 2;  // cool
  t.setpoint_c = 22.5f;
  app.on_controller_telemetry(8000, t);
  ASSERT_NEAR(app.local_cool_setpoint_c(), 22.5f, 0.01f);
  ASSERT_NEAR(app.local_heat_setpoint_c(), 21.5f, 0.01f);
}
#endif
