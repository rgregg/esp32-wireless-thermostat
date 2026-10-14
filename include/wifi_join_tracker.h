#pragma once

#include <stdint.h>

#include <atomic>

// Tracks whether a WiFi join started by WiFi.begin() is still running, so ESP-NOW code
// keeps its hands off the radio channel while the join scans and associates.
//
// A join ends at STA GOT_IP (the STA reports WL_CONNECTED only from then on), at a STA
// disconnect that reports the attempt's failure, or kMaxJoinMs after it began. A
// locally initiated leave (WIFI_REASON_ASSOC_LEAVE) does not end it: WiFi.begin() can
// tear down the previous attempt first, and that event is not this attempt's outcome.
//
// Threading: on_begin()/cancel()/in_progress() run in the main loop; on_got_ip() and
// on_disconnected() run in the WiFi event task. All state is atomic.
class WifiJoinTracker {
 public:
  static constexpr uint32_t kMaxJoinMs = 10000;
  static constexpr uint8_t kReasonAssocLeave = 8;  // WIFI_REASON_ASSOC_LEAVE

  // Call immediately before WiFi.begin().
  void on_begin(uint32_t now_ms) {
    started_ms_.store(now_ms);
    active_.store(true);
  }
  void on_got_ip() { active_.store(false); }
  void on_disconnected(uint8_t reason) {
    if (reason != kReasonAssocLeave) active_.store(false);
  }
  void cancel() { active_.store(false); }

  bool in_progress(uint32_t now_ms) const {
    return active_.load() && static_cast<uint32_t>(now_ms - started_ms_.load()) < kMaxJoinMs;
  }

 private:
  std::atomic<bool> active_{false};
  std::atomic<uint32_t> started_ms_{0};
};
