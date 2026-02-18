#include "controller/controller_app.h"

#include "espnow_cmd_word.h"

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace thermostat {

ControllerApp::ControllerApp(IControllerTransport &transport,
                             const ControllerConfig &config)
    : transport_(transport), config_(config), runtime_(config) {
  indoor_temp_c_ = config_.indoor_temp_fallback_c;
  indoor_humidity_pct_ = config_.indoor_humidity_fallback_pct;
  load_persisted_indoor_fallback();
}

bool ControllerApp::is_newer_u16(uint16_t previous, uint16_t incoming) {
  const uint16_t diff = static_cast<uint16_t>(incoming - previous);
  return diff != 0 && diff < 0x8000;
}

bool ControllerApp::telemetry_payload_changed(const ControllerTelemetry &next) const {
  if (!has_last_published_) {
    return true;
  }
  return next.state != last_published_.state ||
         next.filter_runtime_hours != last_published_.filter_runtime_hours ||
         next.lockout != last_published_.lockout || next.mode_code != last_published_.mode_code ||
         next.fan_code != last_published_.fan_code || next.setpoint_c != last_published_.setpoint_c;
}

void ControllerApp::on_heartbeat(uint32_t now_ms) {
  runtime_.note_heartbeat(now_ms);
}

CommandApplyResult ControllerApp::on_command_word(uint32_t packed_word) {
  const CommandWord cmd = espnow_cmd::decode(packed_word);
  const CommandApplyResult result = runtime_.apply_remote_command(cmd);
  if (result.accepted) {
    publish();
  }
  return result;
}

void ControllerApp::on_thermostat_ack(uint16_t seq) {
  if (!has_last_published_) {
    return;
  }
  if (seq == telemetry_seq_ || is_newer_u16(last_acked_seq_, seq)) {
    last_acked_seq_ = seq;
  }
}

void ControllerApp::set_hvac_lockout(bool locked) {
  runtime_.set_hvac_lockout(locked);
  publish();
}

void ControllerApp::on_indoor_temperature_c(float temp_c) {
  indoor_temp_c_ = temp_c;
  has_indoor_temp_ = true;
  persist_indoor_fallback();
}

void ControllerApp::on_indoor_humidity(float humidity_pct) {
  indoor_humidity_pct_ = humidity_pct;
  has_indoor_humidity_ = true;
  persist_indoor_fallback();
}

void ControllerApp::tick(uint32_t now_ms) {
  bool heat_call = false;
  bool cool_call = false;
  compute_hvac_calls(&heat_call, &cool_call);
  tick(now_ms, heat_call, cool_call);
}

void ControllerApp::tick(uint32_t now_ms, bool heat_call, bool cool_call) {
  ControllerTickInput in;
  in.now_ms = now_ms;
  in.heat_call = heat_call;
  in.cool_call = cool_call;
  runtime_.tick(in);
  publish();
}

uint8_t ControllerApp::mode_to_code(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Heat:
      return 1;
    case FurnaceMode::Cool:
      return 2;
    case FurnaceMode::Off:
    default:
      return 0;
  }
}

uint8_t ControllerApp::fan_to_code(FanMode mode) {
  switch (mode) {
    case FanMode::AlwaysOn:
      return 1;
    case FanMode::Circulate:
      return 2;
    case FanMode::Automatic:
    default:
      return 0;
  }
}

void ControllerApp::compute_hvac_calls(bool *heat_call, bool *cool_call) const {
  if (heat_call == nullptr || cool_call == nullptr) {
    return;
  }

  *heat_call = false;
  *cool_call = false;

  if (!has_indoor_temp_) {
    return;
  }

  const float target = runtime_.target_temperature_c();

  switch (runtime_.mode()) {
    case FurnaceMode::Heat: {
      const bool heat_active = runtime_.heat_demand();
      if (heat_active) {
        *heat_call = indoor_temp_c_ < (target + config_.heat_overrun_c);
      } else {
        *heat_call = indoor_temp_c_ <= (target - config_.heat_deadband_c);
      }
      break;
    }

    case FurnaceMode::Cool: {
      const bool cool_active = runtime_.cool_demand();
      if (cool_active) {
        *cool_call = indoor_temp_c_ > (target - config_.cool_overrun_c);
      } else {
        *cool_call = indoor_temp_c_ >= (target + config_.cool_deadband_c);
      }
      break;
    }

    case FurnaceMode::Off:
    default:
      break;
  }
}

void ControllerApp::publish() {
  ControllerTelemetry t;
  t.state = runtime_.furnace_state();
  t.filter_runtime_hours = runtime_.filter_runtime_hours();
  t.lockout = runtime_.hvac_lockout();
  t.mode_code = mode_to_code(runtime_.mode());
  t.fan_code = fan_to_code(runtime_.fan_mode());
  t.setpoint_c = runtime_.target_temperature_c();

  if (telemetry_payload_changed(t)) {
    telemetry_seq_ = static_cast<uint16_t>(telemetry_seq_ + 1);
    if (telemetry_seq_ == 0) {
      telemetry_seq_ = 1;
    }
  }
  t.seq = telemetry_seq_;

  last_published_ = t;
  has_last_published_ = true;
  transport_.publish_telemetry(t);
}

void ControllerApp::load_persisted_indoor_fallback() {
#if defined(ARDUINO)
  Preferences prefs;
  if (prefs.begin("thermostat", true)) {
    indoor_temp_c_ = prefs.getFloat("indoor_t", indoor_temp_c_);
    indoor_humidity_pct_ = prefs.getFloat("indoor_h", indoor_humidity_pct_);
    prefs.end();
  }
#endif
}

void ControllerApp::persist_indoor_fallback() const {
#if defined(ARDUINO)
  Preferences prefs;
  if (!prefs.begin("thermostat", false)) {
    return;
  }
  prefs.putFloat("indoor_t", indoor_temp_c_);
  prefs.putFloat("indoor_h", indoor_humidity_pct_);
  prefs.end();
#endif
}

}  // namespace thermostat
