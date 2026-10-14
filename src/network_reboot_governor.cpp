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

uint32_t NetworkRebootGovernor::ms_until_reboot(uint32_t now_ms) const {
  if (!isolating_ || reboot_issued_) return 0;
  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);
  const uint32_t threshold = reboot_threshold_ms();
  return elapsed >= threshold ? 0 : threshold - elapsed;
}

GovernorAction NetworkRebootGovernor::poll(uint32_t now_ms, bool isolated,
                                           bool network_was_ready) {
  if (!isolated) {
    isolating_ = false;
    recovered_ = false;
    reboot_issued_ = false;
    if (!healthy_) {
      healthy_ = true;
      healthy_since_ms_ = now_ms;
    } else if (reboot_count_ > 0 &&
               static_cast<uint32_t>(now_ms - healthy_since_ms_) >= config_.healthy_reset_ms) {
      reboot_count_ = 0;
      count_reset_pending_ = true;
    }
    return GovernorAction::None;
  }

  healthy_ = false;

  // Do not count isolation on a boot that has never had a usable network, unless we
  // already know we are in an outage (a previous isolation reboot was recorded).
  if (!network_was_ready && reboot_count_ == 0) {
    isolating_ = false;
    return GovernorAction::None;
  }

  if (!isolating_) {
    isolating_ = true;
    isolation_start_ms_ = now_ms;
    recovered_ = false;
    reboot_issued_ = false;
  }

  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);

  if (!reboot_issued_ && elapsed >= reboot_threshold_ms()) {
    reboot_issued_ = true;
    ++reboot_count_;
    return GovernorAction::Reboot;
  }
  if (!recovered_ && elapsed >= config_.recover_after_ms) {
    recovered_ = true;
    return GovernorAction::RecoverInPlace;
  }
  return GovernorAction::None;
}

}  // namespace thermostat
