#include "controller/controller_runtime.h"

#include <string.h>

#include "espnow_cmd_word.h"
#include "thermostat/thermostat_state.h"

namespace {

constexpr uint32_t kOneMinuteMs = 60000;

}  // namespace

namespace thermostat {

ControllerRuntime::ControllerRuntime(const ControllerConfig &config)
    : config_(config) {
  last_heating_off_ms_ = 0;
  last_cooling_off_ms_ = 0;
  hvac_state_since_ms_ = 0;
}

void ControllerRuntime::note_heartbeat(uint32_t now_ms) {
  heartbeat_last_seen_ms_ = now_ms;
}

void ControllerRuntime::set_hvac_lockout(bool locked_out) {
  hvac_lockout_ = locked_out;
  if (hvac_lockout_) {
    hvac_state_ = HvacState::Idle;
    relay_.fan = false;
    relay_.heat = false;
    relay_.cool = false;
  }
}

void ControllerRuntime::reset_remote_command_sequence() {
  last_seq_default_ = 0;
  for (int i = 0; i < kMaxCommandSources; ++i) {
    command_sources_[i].active = false;
    command_sources_[i].last_seq = 0;
  }
}

CommandApplyResult ControllerRuntime::apply_remote_command(const CommandWord &cmd,
                                                           const uint8_t *source_mac) {
  CommandApplyResult result;

  // Resolve the per-source sequence counter
  uint16_t *last_seq = &last_seq_default_;

  if (source_mac != nullptr) {
    // Find existing entry or allocate a new one
    int free_slot = -1;
    for (int i = 0; i < kMaxCommandSources; ++i) {
      if (command_sources_[i].active &&
          memcmp(command_sources_[i].mac, source_mac, 6) == 0) {
        last_seq = &command_sources_[i].last_seq;
        free_slot = -2;  // sentinel: found
        break;
      }
      if (!command_sources_[i].active && free_slot == -1) {
        free_slot = i;
      }
    }
    if (free_slot >= 0) {
      // New source — register it
      memcpy(command_sources_[free_slot].mac, source_mac, 6);
      command_sources_[free_slot].last_seq = 0;
      command_sources_[free_slot].active = true;
      last_seq = &command_sources_[free_slot].last_seq;
    }
  }

  if (*last_seq != 0) {
    if (!espnow_cmd::is_newer_seq(*last_seq, cmd.seq)) {
      result.stale_or_duplicate = true;
      return result;
    }
  }

  *last_seq = cmd.seq;
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
  apply_hvac_calls(in.now_ms, in.heat_call, in.cool_call);
  run_minute_tasks(in.now_ms);
  enforce_safety_interlocks(in.now_ms);
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

void ControllerRuntime::enforce_safety_interlocks(uint32_t now_ms) {
  if (failsafe_active_ || hvac_lockout_) {
    enter_idle(now_ms);
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

void ControllerRuntime::apply_hvac_calls(uint32_t now_ms, bool heat_call, bool cool_call) {
  if (failsafe_active_ || hvac_lockout_) {
    enter_idle(now_ms);
    relay_.fan = false;
    return;
  }

  switch (hvac_state_) {
    case HvacState::Heating: {
      const bool min_run_done =
          elapsed_at_least(now_ms, hvac_state_since_ms_, config_.min_heating_run_time_ms);
      if (!heat_call && min_run_done) {
        enter_idle(now_ms);
      } else {
        relay_.heat = true;
        relay_.cool = false;
      }
      break;
    }
    case HvacState::Cooling: {
      const bool min_run_done =
          elapsed_at_least(now_ms, hvac_state_since_ms_, config_.min_cooling_run_time_ms);
      if (!cool_call && min_run_done) {
        enter_idle(now_ms);
      } else {
        relay_.heat = false;
        relay_.cool = true;
      }
      break;
    }
    case HvacState::Idle:
    default: {
      const bool idle_ready = elapsed_at_least(now_ms, hvac_state_since_ms_, config_.min_idle_time_ms);
      const bool heat_off_ready =
          !has_heating_off_timestamp_ ||
          elapsed_at_least(now_ms, last_heating_off_ms_, config_.min_heating_off_time_ms);
      const bool cool_off_ready =
          !has_cooling_off_timestamp_ ||
          elapsed_at_least(now_ms, last_cooling_off_ms_, config_.min_cooling_off_time_ms);

      if (heat_call && idle_ready && heat_off_ready) {
        enter_heating(now_ms);
      } else if (cool_call && idle_ready && cool_off_ready) {
        enter_cooling(now_ms);
      } else {
        relay_.heat = false;
        relay_.cool = false;
      }
      break;
    }
  }

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

bool ControllerRuntime::elapsed_at_least(uint32_t now_ms,
                                         uint32_t start_ms,
                                         uint32_t duration_ms) const {
  if (duration_ms == 0) {
    return true;
  }
  return static_cast<uint32_t>(now_ms - start_ms) >= duration_ms;
}

void ControllerRuntime::enter_idle(uint32_t now_ms) {
  if (hvac_state_ == HvacState::Heating) {
    last_heating_off_ms_ = now_ms;
    has_heating_off_timestamp_ = true;
  } else if (hvac_state_ == HvacState::Cooling) {
    last_cooling_off_ms_ = now_ms;
    has_cooling_off_timestamp_ = true;
  }

  hvac_state_ = HvacState::Idle;
  hvac_state_since_ms_ = now_ms;
  relay_.heat = false;
  relay_.cool = false;
}

void ControllerRuntime::enter_heating(uint32_t now_ms) {
  hvac_state_ = HvacState::Heating;
  hvac_state_since_ms_ = now_ms;
  relay_.heat = true;
  relay_.cool = false;
  relay_.fan = false;
}

void ControllerRuntime::enter_cooling(uint32_t now_ms) {
  hvac_state_ = HvacState::Cooling;
  hvac_state_since_ms_ = now_ms;
  relay_.heat = false;
  relay_.cool = true;
  relay_.fan = false;
}

}  // namespace thermostat
