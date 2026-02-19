#include "thermostat/thermostat_app.h"

namespace thermostat {

ThermostatApp::ThermostatApp(IThermostatTransport &transport,
                             const ThermostatAppConfig &config)
    : transport_(transport), config_(config) {}

bool ThermostatApp::is_newer_u16(uint16_t previous, uint16_t incoming) {
  const uint16_t diff = static_cast<uint16_t>(incoming - previous);
  return diff != 0 && diff < 0x8000;
}

void ThermostatApp::on_controller_heartbeat(uint32_t now_ms) {
  last_controller_heartbeat_ms_ = now_ms;
}

void ThermostatApp::on_controller_telemetry(
    uint32_t now_ms, const ThermostatControllerTelemetry &telemetry) {
  if (has_controller_seq_) {
    if (telemetry.seq == last_controller_seq_) {
      ack_controller_seq(last_controller_seq_);
      return;
    }
    if (!is_newer_u16(last_controller_seq_, telemetry.seq)) {
      ack_controller_seq(last_controller_seq_);
      return;
    }
  }

  last_controller_seq_ = telemetry.seq;
  has_controller_seq_ = true;

  has_controller_telemetry_ = true;
  controller_state_ = telemetry.state;
  controller_lockout_ = telemetry.lockout;
  controller_setpoint_c_ = telemetry.setpoint_c;
  controller_filter_runtime_seconds_ = telemetry.filter_runtime_seconds;

  const bool within_debounce =
      (last_local_interaction_ms_ != 0) &&
      (static_cast<uint32_t>(now_ms - last_local_interaction_ms_) <
       config_.local_interaction_debounce_ms);

  if (!within_debounce) {
    local_mode_ = mode_from_code(telemetry.mode_code);
    local_fan_mode_ = fan_from_code(telemetry.fan_code);
    local_setpoint_c_ = telemetry.setpoint_c;
  }

  ack_controller_seq(last_controller_seq_);
}

void ThermostatApp::set_local_mode(FurnaceMode mode, uint32_t now_ms) {
  local_mode_ = mode;
  mark_local_interaction(now_ms);
  send_command(false, false);
}

void ThermostatApp::set_local_fan_mode(FanMode mode, uint32_t now_ms) {
  local_fan_mode_ = mode;
  mark_local_interaction(now_ms);
  send_command(false, false);
}

void ThermostatApp::set_local_setpoint_c(float setpoint_c, uint32_t now_ms) {
  if (setpoint_c < 0.0f) {
    setpoint_c = 0.0f;
  }
  if (setpoint_c > 40.0f) {
    setpoint_c = 40.0f;
  }
  local_setpoint_c_ = setpoint_c;
  mark_local_interaction(now_ms);
  send_command(false, false);
}

void ThermostatApp::request_sync(uint32_t now_ms) {
  mark_local_interaction(now_ms);
  send_command(true, false);
}

void ThermostatApp::request_filter_reset(uint32_t now_ms) {
  mark_local_interaction(now_ms);
  send_command(false, true);
}

void ThermostatApp::publish_indoor_temperature_c(float temp_c) {
  transport_.publish_indoor_temperature_c(temp_c);
}

void ThermostatApp::publish_indoor_humidity(float humidity_pct) {
  transport_.publish_indoor_humidity(humidity_pct);
}

bool ThermostatApp::controller_connected(uint32_t now_ms, uint32_t timeout_ms) const {
  if (last_controller_heartbeat_ms_ == 0) {
    return false;
  }
  return static_cast<uint32_t>(now_ms - last_controller_heartbeat_ms_) < timeout_ms;
}

FurnaceMode ThermostatApp::mode_from_code(uint8_t mode_code) {
  switch (mode_code) {
    case 1:
      return FurnaceMode::Heat;
    case 2:
      return FurnaceMode::Cool;
    case 0:
    default:
      return FurnaceMode::Off;
  }
}

FanMode ThermostatApp::fan_from_code(uint8_t fan_code) {
  switch (fan_code) {
    case 1:
      return FanMode::AlwaysOn;
    case 2:
      return FanMode::Circulate;
    case 0:
    default:
      return FanMode::Automatic;
  }
}

void ThermostatApp::mark_local_interaction(uint32_t now_ms) {
  last_local_interaction_ms_ = now_ms;
}

void ThermostatApp::ack_controller_seq(uint16_t seq) {
  transport_.publish_controller_ack(seq);
}

void ThermostatApp::send_command(bool do_sync, bool do_filter_reset) {
  CommandWord cmd;
  cmd.mode = local_mode_;
  cmd.fan = local_fan_mode_;
  int setpoint_decic = static_cast<int>(local_setpoint_c_ * 10.0f + 0.5f);
  if (setpoint_decic < 0) {
    setpoint_decic = 0;
  }
  if (setpoint_decic > 400) {
    setpoint_decic = 400;
  }
  cmd.setpoint_decic = static_cast<uint16_t>(setpoint_decic);

  seq_local_ = espnow_cmd::next_seq(seq_local_);
  cmd.seq = seq_local_;
  cmd.sync_request = do_sync;
  cmd.filter_reset = do_filter_reset;

  last_packed_command_ = espnow_cmd::encode(cmd);
  last_command_seq_ = cmd.seq;
  has_last_packed_command_ = true;
  transport_.publish_command_word(last_packed_command_);
}

}  // namespace thermostat
