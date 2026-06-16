#include "controller/network_recovery_policy.h"

namespace thermostat {

RecoveryAction NetworkRecoveryPolicy::poll(uint32_t now_ms) {
  // Reboot is a sticky/terminal signal: once requested, keep returning Reboot
  // until on_connected() clears it (defensive reset only — caller should reboot).
  if (reboot_requested_) {
    return RecoveryAction::Reboot;
  }

  if (connected_) {
    return RecoveryAction::None;
  }

  // An attempt is in progress — wait for on_connect_failed() or on_connected().
  if (attempt_in_progress_) {
    return RecoveryAction::None;
  }

  // Not yet time for the next attempt — wrap-safe across the uint32 millis rollover.
  // Only applies once on_connect_failed() has set a future retry time.
  if (has_next_attempt_ && static_cast<int32_t>(now_ms - next_attempt_ms_) < 0) {
    return RecoveryAction::None;
  }

  // Check for escalation before issuing the next action.
  if (consecutive_fails_ >= config_.fails_before_restart) {
    consecutive_fails_ = 0;
    restart_count_++;
    backoff_ms_ = 0;
    next_attempt_ms_ = 0;
    has_next_attempt_ = false;

    if (restart_count_ > config_.restarts_before_reboot) {
      reboot_requested_ = true;
      return RecoveryAction::Reboot;
    }
    return RecoveryAction::RestartSubsystem;
  }

  // Issue a connect attempt.
  attempt_in_progress_ = true;
  attempt_start_ms_ = now_ms;
  return RecoveryAction::Connect;
}

void NetworkRecoveryPolicy::on_connected() {
  connected_ = true;
  attempt_in_progress_ = false;
  reboot_requested_ = false;  // defensive: clear sticky Reboot flag on success
  has_next_attempt_ = false;
  consecutive_fails_ = 0;
  restart_count_ = 0;
  backoff_ms_ = 0;
  next_attempt_ms_ = 0;
}

void NetworkRecoveryPolicy::on_connect_failed() {
  attempt_in_progress_ = false;
  consecutive_fails_++;

  // Compute next backoff: base on first fail, doubling each subsequent fail,
  // capped at max_backoff_ms.
  if (backoff_ms_ == 0) {
    backoff_ms_ = config_.base_backoff_ms;
  } else {
    backoff_ms_ *= 2;
    if (backoff_ms_ > config_.max_backoff_ms) {
      backoff_ms_ = config_.max_backoff_ms;
    }
  }

  // Anchor the next attempt relative to when the attempt was *issued*, not
  // when on_connect_failed() is called (on_connect_failed has no clock).
  next_attempt_ms_ = attempt_start_ms_ + backoff_ms_;
  has_next_attempt_ = true;
}

void NetworkRecoveryPolicy::on_disconnected() {
  connected_ = false;
  // A link drop mid-attempt (attempt_in_progress_ == true) is treated as a clean
  // link drop, NOT a connect failure — so we reset consecutive_fails_ and backoff
  // here rather than escalating. The caller should not also call on_connect_failed().
  attempt_in_progress_ = false;
  has_next_attempt_ = false;
  consecutive_fails_ = 0;
  backoff_ms_ = 0;
  next_attempt_ms_ = 0;
  // restart_count_ intentionally NOT reset — caller only resets it via
  // on_connected(). A disconnect that was never connected leaves restart_count_
  // unchanged; on_disconnected() after a successful on_connected() leaves it at
  // 0 since on_connected() already reset it.
}

}  // namespace thermostat
