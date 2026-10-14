#include "network_reboot_governor.h"

namespace thermostat {

uint32_t NetworkRebootGovernor::reboot_threshold_ms() const {
  switch (reboot_count_) {
    case 0:
      return config_.first_reboot_after_ms;
    case 1:
      return config_.second_reboot_after_ms;
    case 2:
      return config_.third_reboot_after_ms;
    default:
      return config_.later_reboot_after_ms;
  }
}

bool NetworkRebootGovernor::take_count_reset() {
  const bool pending = count_reset_pending_;
  count_reset_pending_ = false;
  return pending;
}

// True when `period` has passed since `mark`; then moves `mark` forward by whole periods
// so it stays within one period of now (due times stay on the original grid, and the
// arithmetic stays wraparound-safe however long the condition lasts).
bool NetworkRebootGovernor::take_due(uint32_t now_ms, uint32_t &mark, uint32_t period) {
  const uint32_t since = static_cast<uint32_t>(now_ms - mark);
  if (period == 0 || since < period) return false;
  mark += period * (since / period);
  return true;
}

uint32_t NetworkRebootGovernor::ms_until_reboot(uint32_t now_ms) const {
  if (!isolating_ || reboot_issued_) return 0;
  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);
  const uint32_t threshold = reboot_threshold_ms();
  return elapsed >= threshold ? 0 : threshold - elapsed;
}

GovernorAction NetworkRebootGovernor::poll(uint32_t now_ms, bool isolated, bool mqtt_up,
                                           bool network_was_ready) {
  // Neither clock runs on a boot that has never had a usable network, unless we already
  // know we are in an outage (a previous isolation reboot was recorded).
  const bool gated = !network_was_ready && reboot_count_ == 0;

  // MQTT-down recovery schedule. A due time is consumed whether or not it fires, so one
  // that falls while isolated is skipped rather than delivered late.
  bool mqtt_recover_due = false;
  if (mqtt_up || gated) {
    mqtt_down_ = false;
  } else {
    if (!mqtt_down_) {
      mqtt_down_ = true;
      mqtt_recover_mark_ms_ = now_ms;
    }
    mqtt_recover_due = take_due(now_ms, mqtt_recover_mark_ms_, config_.mqtt_down_recover_ms);
  }

  if (!isolated) {
    isolating_ = false;
    reboot_issued_ = false;
    if (!healthy_) {
      healthy_ = true;
      healthy_since_ms_ = now_ms;
    } else if (reboot_count_ > 0 &&
               static_cast<uint32_t>(now_ms - healthy_since_ms_) >= config_.healthy_reset_ms) {
      reboot_count_ = 0;
      count_reset_pending_ = true;
    }
    return mqtt_recover_due ? GovernorAction::RecoverInPlace : GovernorAction::None;
  }

  healthy_ = false;

  if (gated) {
    isolating_ = false;
    return GovernorAction::None;
  }

  if (!isolating_) {
    isolating_ = true;
    isolation_start_ms_ = now_ms;
    recover_mark_ms_ = now_ms;
    recover_wait_ms_ = config_.recover_after_ms;
    reboot_issued_ = false;
  }

  // Once the reboot is issued the device is restarting; nothing more to do.
  if (reboot_issued_) return GovernorAction::None;

  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);
  const bool recover_due = take_due(now_ms, recover_mark_ms_, recover_wait_ms_);
  if (recover_due) recover_wait_ms_ = config_.recover_repeat_ms;

  if (elapsed >= reboot_threshold_ms()) {
    reboot_issued_ = true;
    ++reboot_count_;
    return GovernorAction::Reboot;  // wins over a recovery due at the same poll
  }
  return recover_due ? GovernorAction::RecoverInPlace : GovernorAction::None;
}

}  // namespace thermostat
