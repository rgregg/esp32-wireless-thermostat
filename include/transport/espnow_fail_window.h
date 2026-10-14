#pragma once

#include <stdint.h>

namespace thermostat {

// Turns a monotonically increasing failure counter into "has there been a failure in
// the last window_ms?". Used so the ESP-NOW error diagnostic clears itself instead of
// latching on the first failed send since boot.
class RecentFailWindow {
 public:
  explicit RecentFailWindow(uint32_t window_ms = 300000) : window_ms_(window_ms) {}

  void update(uint32_t now_ms, uint32_t total_fail_count) {
    if (total_fail_count != last_total_) {
      last_total_ = total_fail_count;
      has_fail_ = true;
      last_fail_ms_ = now_ms;
    }
  }

  bool recent(uint32_t now_ms) const {
    return has_fail_ && static_cast<uint32_t>(now_ms - last_fail_ms_) < window_ms_;
  }

 private:
  uint32_t window_ms_;
  uint32_t last_total_ = 0;
  bool has_fail_ = false;
  uint32_t last_fail_ms_ = 0;
};

}  // namespace thermostat
