#include "controller_runtime.h"

#include "espnow_cmd_word.h"
#include "thermostat_state.h"

namespace {

constexpr uint32_t kOneMinuteMs = 60000;

bool elapsed_at_least(uint32_t now, uint32_t then, uint32_t interval_ms) {
  return static_cast<uint32_t>(now - then) >= interval_ms;
}

}  // namespace

namespace thermostat {

ControllerRuntime::ControllerRuntime(const ControllerConfig &config)
    : config_(config) {}

void ControllerRuntime::note_heartbeat(uint32_t now_ms) {
  heartbeat_last_seen_ms_ = now_ms;
}

void ControllerRuntime::set_hvac_lockout(bool locked_out) {
  hvac_lockout_ = locked_out;
  if (hvac_lockout_) {
    relay_.heat = false;
    relay_.cool = false;
    relay_.fan = false;
  }
}

CommandApplyResult ControllerRuntime::apply_remote_command(const CommandWord &cmd) {
  CommandApplyResult result;

  if (last_seq_ != 0) {
    if (!espnow_cmd::is_newer_seq(last_seq_, cmd.seq)) {
      result.stale_or_duplicate = true;
      return result;
    }
  }

  last_seq_ = cmd.seq;
  result.accepted = true;

  if (cmd.sync_request) {
    result.sync_requested = true;
    return result;
  }

  mode_ = cmd.mode;
  fan_mode_ = cmd.fan;
  target_temperature_c_ = static_cast<float>(cmd.setpoint_decic) / 10.0f;

  if (cmd.filter_reset) {
    filter_runtime_seconds_ = 0;
    result.filter_reset_requested = true;
  }

  return result;
}

void ControllerRuntime::tick(const ControllerTickInput &in) {
  update_failsafe(in.now_ms);
  apply_hvac_calls(in.heat_call, in.cool_call);
  run_minute_tasks(in.now_ms);
  enforce_safety_interlocks();
}

FurnaceStateCode ControllerRuntime::furnace_state() const {
  return compute_furnace_state(snapshot());
}

ThermostatSnapshot ControllerRuntime::snapshot() const {
  ThermostatSnapshot s;
  s.mode = mode_;
  s.fan_mode = fan_mode_;
  s.relay = relay_;
  s.hvac_lockout = hvac_lockout_;
  s.failsafe_active = failsafe_active_;
  return s;
}

void ControllerRuntime::enforce_safety_interlocks() {
  if (failsafe_active_ || hvac_lockout_) {
    relay_.heat = false;
    relay_.cool = false;
    relay_.fan = false;
  }

  if (relay_.heat) {
    relay_.cool = false;
    relay_.fan = false;
  } else if (relay_.cool) {
    relay_.heat = false;
    relay_.fan = false;
  }
}

void ControllerRuntime::update_failsafe(uint32_t now_ms) {
  failsafe_active_ = is_failsafe_timed_out(now_ms, heartbeat_last_seen_ms_,
                                           config_.failsafe_timeout_ms);
}

void ControllerRuntime::apply_hvac_calls(bool heat_call, bool cool_call) {
  if (failsafe_active_ || hvac_lockout_) {
    relay_.heat = false;
    relay_.cool = false;
    relay_.fan = false;
    return;
  }

  if (heat_call) {
    relay_.heat = true;
    relay_.cool = false;
    relay_.fan = false;
    return;
  }

  if (cool_call) {
    relay_.heat = false;
    relay_.cool = true;
    relay_.fan = false;
    return;
  }

  relay_.heat = false;
  relay_.cool = false;

  if (fan_mode_ == FanMode::AlwaysOn && !spare_relay_4_) {
    relay_.fan = true;
  } else if (fan_mode_ != FanMode::Circulate) {
    relay_.fan = false;
  }
}

void ControllerRuntime::run_minute_tasks(uint32_t now_ms) {
  if (last_minute_tick_ms_ == 0) {
    last_minute_tick_ms_ = now_ms;
    return;
  }

  while (elapsed_at_least(now_ms, last_minute_tick_ms_, kOneMinuteMs)) {
    last_minute_tick_ms_ += kOneMinuteMs;

    if (relay_.heat || relay_.cool || relay_.fan || spare_relay_4_) {
      filter_runtime_seconds_ += 60;
    }

    if (!failsafe_active_ && !hvac_lockout_ && fan_mode_ == FanMode::Circulate) {
      const int period = static_cast<int>(config_.fan_circulate_period_min);
      const int duration = static_cast<int>(config_.fan_circulate_duration_min);
      if (period > 0 && duration > 0) {
        ++fan_circulate_elapsed_min_;
        const bool window_open = (fan_circulate_elapsed_min_ % period) < duration;
        if (window_open && !relay_.heat && !relay_.cool && !spare_relay_4_) {
          relay_.fan = true;
        } else {
          relay_.fan = false;
        }
      }
    } else {
      fan_circulate_elapsed_min_ = 0;
    }
  }
}

}  // namespace thermostat
