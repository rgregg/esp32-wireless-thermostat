#pragma once

#include <stdint.h>

namespace thermostat {

// Turns a monotonically increasing failure counter into "has there been a failure in
// the last window_ms?". Used so the ESP-NOW error diagnostic clears itself instead of
// latching on the first failed send since boot.
//
// update() must be polled regularly with the transport's running fail count; a failure
// is only seen at the poll that observes the increase. The first update() seeds the
// baseline and never registers a failure. A counter that goes backwards (transport
// restart) or jumps by more than 0x7FFFFFFF only re-baselines.
class RecentFailWindow {
 public:
  explicit RecentFailWindow(uint32_t window_ms = 300000) : window_ms_(window_ms) {}

  void update(uint32_t now_ms, uint32_t total_fail_count) {
    if (!seeded_) {
      seeded_ = true;
    } else if (total_fail_count != last_total_) {
      const uint32_t delta = total_fail_count - last_total_;  // unsigned, wrap-safe
      if (delta >= 1 && delta <= 0x7FFFFFFFu) {
        has_fail_ = true;
        last_fail_ms_ = now_ms;
      }
    }
    last_total_ = total_fail_count;

    // Expire the failure once the window has elapsed so an old failure cannot read as
    // recent again after the millis() wrap.
    if (has_fail_ && elapsed(now_ms) >= window_ms_) {
      has_fail_ = false;
    }
  }

  bool recent(uint32_t now_ms) const {
    return has_fail_ && elapsed(now_ms) < window_ms_;
  }

 private:
  uint32_t elapsed(uint32_t now_ms) const {
    return static_cast<uint32_t>(now_ms - last_fail_ms_);
  }

  uint32_t window_ms_;
  uint32_t last_total_ = 0;
  bool seeded_ = false;
  bool has_fail_ = false;
  uint32_t last_fail_ms_ = 0;
};

}  // namespace thermostat
