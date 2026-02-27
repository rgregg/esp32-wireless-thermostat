#include "controller/controller_app.h"

#include <cmath>
#include <string.h>

#include "espnow_cmd_word.h"

#if defined(ARDUINO)
#include <Preferences.h>
#endif

namespace thermostat {

namespace {
bool is_broadcast_mac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; ++i) {
    if (mac[i] != 0xFF) return false;
  }
  return true;
}
}  // namespace

ControllerApp::ControllerApp(IControllerTransport &transport,
                             const ControllerConfig &config)
    : transport_(transport), config_(config), runtime_(config) {
  indoor_temp_c_ = config_.indoor_temp_fallback_c;
  indoor_humidity_pct_ = config_.indoor_humidity_fallback_pct;
  has_indoor_temp_ = std::isfinite(indoor_temp_c_);
  load_persisted_state();
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

CommandApplyResult ControllerApp::on_command_word(uint32_t packed_word,
                                                  const uint8_t *source_mac) {
  const CommandWord cmd = espnow_cmd::decode(packed_word);
  const CommandApplyResult result = runtime_.apply_remote_command(cmd, source_mac);
  if (result.accepted) {
    if (result.filter_reset_requested) {
      maybe_persist_filter_runtime(/*force=*/true);
    }
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

void ControllerApp::reset_remote_command_sequence() {
  runtime_.reset_remote_command_sequence();
}

void ControllerApp::on_indoor_temperature_c(float temp_c, const uint8_t *src_mac) {
  if (src_mac != nullptr) {
    if (is_broadcast_mac(primary_sensor_mac_)) {
      // Auto-claim: first ESP-NOW source becomes primary
      memcpy(primary_sensor_mac_, src_mac, 6);
      primary_sensor_auto_claimed_ = true;
    } else if (memcmp(src_mac, primary_sensor_mac_, 6) != 0) {
      return;  // Not the primary sensor — ignore
    }
  }
  indoor_temp_c_ = temp_c;
  has_indoor_temp_ = true;
}

void ControllerApp::on_indoor_humidity(float humidity_pct, const uint8_t *src_mac) {
  if (src_mac != nullptr) {
    if (is_broadcast_mac(primary_sensor_mac_)) {
      memcpy(primary_sensor_mac_, src_mac, 6);
      primary_sensor_auto_claimed_ = true;
    } else if (memcmp(src_mac, primary_sensor_mac_, 6) != 0) {
      return;
    }
  }
  indoor_humidity_pct_ = humidity_pct;
  has_indoor_humidity_ = true;
}

void ControllerApp::set_primary_sensor_mac(const uint8_t *mac) {
  if (mac != nullptr) {
    memcpy(primary_sensor_mac_, mac, 6);
  } else {
    // Reset to broadcast (accept all)
    memset(primary_sensor_mac_, 0xFF, 6);
  }
  primary_sensor_auto_claimed_ = false;
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
  in.has_indoor_temp = has_indoor_temp_;
  runtime_.tick(in);
  maybe_persist_filter_runtime();
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

  if (!telemetry_payload_changed(t)) {
    return;  // Nothing changed — skip the send
  }

  telemetry_seq_ = static_cast<uint16_t>(telemetry_seq_ + 1);
  if (telemetry_seq_ == 0) {
    telemetry_seq_ = 1;
  }
  t.seq = telemetry_seq_;

  last_published_ = t;
  has_last_published_ = true;
  transport_.publish_telemetry(t);
}

void ControllerApp::load_persisted_state() {
#if defined(ARDUINO)
  Preferences prefs;
  if (prefs.begin("thermostat", true)) {
    const uint32_t saved_filter = prefs.getUInt("filter_rt", 0);
    runtime_.set_filter_runtime_seconds(saved_filter);
    persisted_filter_runtime_s_ = saved_filter;
    prefs.end();
  }
#endif
}

void ControllerApp::maybe_persist_filter_runtime(bool force) {
#if defined(ARDUINO)
  const uint32_t current = runtime_.filter_runtime_seconds();
  if (current == persisted_filter_runtime_s_) {
    return;
  }
  // Throttle NVS writes to at most once per 10 minutes to reduce flash wear,
  // unless force is true (e.g. filter reset).
  constexpr uint32_t kPersistIntervalMs = 10UL * 60UL * 1000UL;
  const uint32_t now = millis();
  if (!force &&
      static_cast<uint32_t>(now - last_filter_persist_ms_) < kPersistIntervalMs) {
    return;
  }
  Preferences prefs;
  if (!prefs.begin("thermostat", false)) {
    return;
  }
  prefs.putUInt("filter_rt", current);
  prefs.end();
  persisted_filter_runtime_s_ = current;
  last_filter_persist_ms_ = now;
#else
  (void)force;
#endif
}

void ControllerApp::set_outdoor_weather(float temp_c, WeatherIcon icon) {
  outdoor_temp_c_ = temp_c;
  outdoor_icon_ = icon;
  has_outdoor_weather_ = true;
}

}  // namespace thermostat
