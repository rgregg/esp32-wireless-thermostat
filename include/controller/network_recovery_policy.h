#pragma once

#include <stdint.h>

namespace thermostat {

struct NetworkRecoveryConfig {
  uint32_t base_backoff_ms = 1000;
  uint32_t max_backoff_ms = 60000;
  uint32_t fails_before_restart = 5;
  uint32_t restarts_before_reboot = 3;
};

enum class RecoveryAction : uint8_t {
  None = 0,
  Connect = 1,
  RestartSubsystem = 2,
  Reboot = 3,
};

class NetworkRecoveryPolicy {
 public:
  explicit NetworkRecoveryPolicy(const NetworkRecoveryConfig &config = NetworkRecoveryConfig())
      : config_(config) {}

  RecoveryAction poll(uint32_t now_ms);
  void on_connected();
  void on_connect_failed();
  void on_disconnected();

  bool connected() const { return connected_; }
  uint32_t current_backoff_ms() const { return backoff_ms_; }
  uint32_t consecutive_fails() const { return consecutive_fails_; }
  uint32_t restart_count() const { return restart_count_; }

 private:
  NetworkRecoveryConfig config_{};
  bool connected_ = false;
  bool attempt_in_progress_ = false;
  uint32_t consecutive_fails_ = 0;
  uint32_t restart_count_ = 0;
  uint32_t backoff_ms_ = 0;
  uint32_t next_attempt_ms_ = 0;
  uint32_t attempt_start_ms_ = 0;
  bool started_ = false;
};

}  // namespace thermostat
