#pragma once

#include <stdint.h>

#include <atomic>

// Tracks whether a WiFi join is running, so ESP-NOW code keeps its hands off the radio
// channel while the join scans and associates.
//
// A join starts only when the connect was actually started (try_begin()). It ends at
// STA GOT_IP (the STA reports WL_CONNECTED only from then on), at a STA disconnect that
// reports the attempt's failure, or kMaxJoinMs after it began. A locally initiated leave
// (WIFI_REASON_ASSOC_LEAVE) does not end it: starting a connect can tear down the previous
// attempt first, and that event is not this attempt's outcome.
//
// Threading: try_begin()/on_begin()/cancel()/in_progress() run in the main loop;
// on_got_ip() and on_disconnected() run in the WiFi event task. All state is atomic.
class WifiJoinTracker {
 public:
  static constexpr uint32_t kMaxJoinMs = 10000;
  static constexpr uint8_t kReasonAssocLeave = 8;  // WIFI_REASON_ASSOC_LEAVE

  enum class Start : uint8_t { Started, Failed, Busy };

  // Starts one join attempt unless one is already running (Busy: nothing is called, so
  // attempts never overlap and the running join's 10 s cap is the backstop). start() must
  // return true only when the connect was really started (esp_wifi_connect called);
  // otherwise no event would ever end the join, so it is cancelled (Failed).
  template <typename StartFn>
  Start try_begin(uint32_t now_ms, StartFn start) {
    if (in_progress(now_ms)) return Start::Busy;
    on_begin(now_ms);  // before start(): its events may arrive at once
    if (!start()) {
      cancel();
      return Start::Failed;
    }
    return Start::Started;
  }

  void on_begin(uint32_t now_ms) {
    started_ms_.store(now_ms);
    active_.store(true);
  }
  void on_got_ip() { active_.store(false); }
  void on_disconnected(uint8_t reason, uint32_t now_ms) {
    if (reason == kReasonAssocLeave) return;
    // arduino-esp32 reconnects by itself once, after the first non-LEAVE disconnect since
    // boot (`first_connect` in _onStaArduinoEvent, libraries/WiFi/src/STA.cpp), even with
    // auto-reconnect off. That disconnect therefore starts a join rather than ending one.
    if (!framework_retry_used_.exchange(true)) {
      on_begin(now_ms);
      return;
    }
    active_.store(false);
  }
  void cancel() { active_.store(false); }

  bool in_progress(uint32_t now_ms) const {
    return active_.load() && static_cast<uint32_t>(now_ms - started_ms_.load()) < kMaxJoinMs;
  }

 private:
  std::atomic<bool> active_{false};
  std::atomic<uint32_t> started_ms_{0};
  std::atomic<bool> framework_retry_used_{false};
};
