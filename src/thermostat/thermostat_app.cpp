#include "thermostat/thermostat_app.h"

#include "command_builder.h"

namespace thermostat {

ThermostatApp::ThermostatApp(IThermostatTransport &transport,
                             const ThermostatAppConfig &config)
    : transport_(transport), config_(config) {}

float ThermostatApp::local_setpoint_c() const {
  // Cool mode shows cool bin; Heat and Off show heat bin.
  if (local_mode_ == FurnaceMode::Cool) return local_cool_setpoint_c_;
  return local_heat_setpoint_c_;
}

namespace {
// Sync incoming setpoint into the bin matching the given mode.
// For Off, leave both bins unchanged.
void sync_setpoint_into_bin(FurnaceMode mode, float setpoint_c,
                            float &heat_bin, float &cool_bin) {
  if (mode == FurnaceMode::Heat) heat_bin = setpoint_c;
  else if (mode == FurnaceMode::Cool) cool_bin = setpoint_c;
}
}  // namespace

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
  controller_windows_open_ = telemetry.windows_open;
  controller_setpoint_c_ = telemetry.setpoint_c;
  controller_filter_runtime_seconds_ = telemetry.filter_runtime_seconds;

  const bool within_debounce =
      (last_local_interaction_ms_ != 0) &&
      (static_cast<uint32_t>(now_ms - last_local_interaction_ms_) <
       config_.local_interaction_debounce_ms);

  if (!within_debounce) {
    const FurnaceMode tmode = mode_from_code(telemetry.mode_code);
    local_mode_ = tmode;
    local_fan_mode_ = fan_from_code(telemetry.fan_code);
    sync_setpoint_into_bin(tmode, telemetry.setpoint_c,
                           local_heat_setpoint_c_, local_cool_setpoint_c_);
  }

  ack_controller_seq(last_controller_seq_);
}

void ThermostatApp::on_controller_state_update(
    uint32_t now_ms, FurnaceStateCode state, bool lockout, FurnaceMode mode,
    FanMode fan, float setpoint_c, uint32_t filter_runtime_seconds,
    bool windows_open) {
  on_controller_heartbeat(now_ms);

  has_controller_telemetry_ = true;
  controller_state_ = state;
  controller_lockout_ = lockout;
  controller_windows_open_ = windows_open;
  controller_setpoint_c_ = setpoint_c;
  controller_filter_runtime_seconds_ = filter_runtime_seconds;

  const bool within_debounce =
      (last_local_interaction_ms_ != 0) &&
      (static_cast<uint32_t>(now_ms - last_local_interaction_ms_) <
       config_.local_interaction_debounce_ms);

  if (!within_debounce) {
    local_mode_ = mode;
    local_fan_mode_ = fan;
    sync_setpoint_into_bin(mode, setpoint_c,
                           local_heat_setpoint_c_, local_cool_setpoint_c_);
  }
}

void ThermostatApp::set_local_mode(FurnaceMode mode, uint32_t now_ms) {
  local_mode_ = mode;
  mark_local_interaction(now_ms);
  // Mode-only change: keep controller's stored fan and setpoint authoritative.
  SendOptions opts;
  opts.preserve_fan = true;
  opts.preserve_setpoint = true;
  send_command(opts);
}

void ThermostatApp::set_local_fan_mode(FanMode mode, uint32_t now_ms) {
  local_fan_mode_ = mode;
  mark_local_interaction(now_ms);
  SendOptions opts;
  opts.preserve_mode = true;
  opts.preserve_setpoint = true;
  send_command(opts);
}

void ThermostatApp::set_local_setpoint_c(float setpoint_c, uint32_t now_ms) {
  if (setpoint_c < 0.0f) {
    setpoint_c = 0.0f;
  }
  if (setpoint_c > 40.0f) {
    setpoint_c = 40.0f;
  }
  // Adjust the bin for the active mode. Off → heat bin (matches local_setpoint_c()).
  if (local_mode_ == FurnaceMode::Cool) {
    local_cool_setpoint_c_ = setpoint_c;
  } else {
    local_heat_setpoint_c_ = setpoint_c;
  }
  mark_local_interaction(now_ms);
  SendOptions opts;
  opts.preserve_mode = true;
  opts.preserve_fan = true;
  send_command(opts);
}

void ThermostatApp::request_sync(uint32_t now_ms) {
  mark_local_interaction(now_ms);
  SendOptions opts;
  opts.sync_request = true;
  send_command(opts);
}

void ThermostatApp::request_filter_reset(uint32_t now_ms) {
  mark_local_interaction(now_ms);
  SendOptions opts;
  opts.filter_reset = true;
  // Filter reset is independent of HVAC settings — preserve everything.
  opts.preserve_mode = true;
  opts.preserve_fan = true;
  opts.preserve_setpoint = true;
  send_command(opts);
}

void ThermostatApp::reset_local_command_sequence() {
  seq_local_ = 0;
  has_last_packed_command_ = false;
  last_packed_command_ = 0;
  last_command_seq_ = 0;
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

void ThermostatApp::send_command(const SendOptions &opts) {
  seq_local_ = espnow_cmd::next_seq(seq_local_);
  CommandWord cmd = build_command_word(local_mode_, local_fan_mode_, local_setpoint_c(),
                                       seq_local_, opts.sync_request, opts.filter_reset);
  cmd.preserve_mode = opts.preserve_mode;
  cmd.preserve_fan = opts.preserve_fan;
  cmd.preserve_setpoint = opts.preserve_setpoint;

  last_packed_command_ = espnow_cmd::encode(cmd);
  last_command_seq_ = cmd.seq;
  has_last_packed_command_ = true;
  transport_.publish_command_word(last_packed_command_);
}

}  // namespace thermostat
