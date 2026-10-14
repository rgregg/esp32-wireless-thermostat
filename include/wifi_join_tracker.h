#pragma once

#include <stdint.h>

#include <atomic>

// Tracks whether a WiFi join is running, so ESP-NOW code keeps its hands off the radio
// channel while the join scans and associates, and decides when an STA that associated
// but never got an IP has failed.
//
// A join starts only when the connect was actually started (try_begin()). It ends at
// STA GOT_IP (the STA reports WL_CONNECTED only from then on), at a STA disconnect that
// reports the attempt's failure, kMaxJoinMs after it began if it has not associated, or
// kAssocNoIpTimeoutMs after it associated (L2) without getting an IP. A locally initiated
// leave (WIFI_REASON_ASSOC_LEAVE) does not end it: starting a connect can tear down the
// previous attempt first, and that event is not this attempt's outcome.
//
// Associated without IP (AP up, DHCP or gateway down): the caller reports the STA's L2
// association through on_no_ip() on every pass while it has no IP. Inside the window no
// new connect may be issued (try_begin() is Busy; arduino-esp32's STA.connect() on an
// associated STA would first tear the association down and re-associate). When the
// window expires on_no_ip() returns TimedOut once: the caller disconnects (a local leave
// the tracker ignores) and waits its normal retry interval before the next attempt. The
// window applies to any association without IP, including one outside a tracked join
// (the IP lease lost while associated, or a join that associated after its 10 s cap).
//
// Threading: try_begin()/on_begin()/on_no_ip()/cancel()/in_progress() run in the main
// loop; on_got_ip() and on_disconnected() run in the WiFi event task. All state is atomic.
class WifiJoinTracker {
 public:
  static constexpr uint32_t kMaxJoinMs = 10000;
  static constexpr uint32_t kAssocNoIpTimeoutMs = 20000;
  static constexpr uint8_t kReasonAssocLeave = 8;  // WIFI_REASON_ASSOC_LEAVE

  enum class Start : uint8_t { Started, Failed, Busy };
  // NotAssociated: the STA is not associated; a join may be started.
  // Waiting: associated without IP, inside the window; leave the STA alone.
  // TimedOut: the window just expired; the join has ended, disconnect the STA.
  enum class NoIp : uint8_t { NotAssociated, Waiting, TimedOut };

  // Starts one join attempt unless one is already running or the STA is associated
  // without IP inside its window (Busy: nothing is called, so attempts never overlap).
  // start() must return true only when the connect was really started (esp_wifi_connect
  // called); otherwise no event would ever end the join, so it is cancelled (Failed).
  template <typename StartFn>
  Start try_begin(uint32_t now_ms, StartFn start) {
    if (in_progress(now_ms) || associated_.load()) return Start::Busy;
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
  void on_got_ip() {
    active_.store(false);
    associated_.store(false);
  }
  void on_disconnected(uint8_t reason, uint32_t now_ms) {
    associated_.store(false);
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
  // Call on every main-loop pass while the STA has no IP; associated is its L2
  // association (WiFi.STA.connected()). See the class comment.
  NoIp on_no_ip(uint32_t now_ms, bool associated) {
    if (!associated) {
      associated_.store(false);
      return NoIp::NotAssociated;
    }
    if (!associated_.load()) {
      associated_ms_.store(now_ms);
      associated_.store(true);
    }
    if (static_cast<uint32_t>(now_ms - associated_ms_.load()) < kAssocNoIpTimeoutMs) {
      return NoIp::Waiting;
    }
    // Failed join. Clearing the association means a disconnect that does not take
    // effect gets a fresh window rather than an immediate second TimedOut.
    active_.store(false);
    associated_.store(false);
    return NoIp::TimedOut;
  }
  void cancel() {
    active_.store(false);
    associated_.store(false);
  }

  bool in_progress(uint32_t now_ms) const {
    if (!active_.load()) return false;
    if (associated_.load()) {
      return static_cast<uint32_t>(now_ms - associated_ms_.load()) < kAssocNoIpTimeoutMs;
    }
    return static_cast<uint32_t>(now_ms - started_ms_.load()) < kMaxJoinMs;
  }

 private:
  std::atomic<bool> active_{false};
  std::atomic<uint32_t> started_ms_{0};
  std::atomic<bool> framework_retry_used_{false};
  // Associated at L2 without an IP, since associated_ms_ (set from the main loop only).
  std::atomic<bool> associated_{false};
  std::atomic<uint32_t> associated_ms_{0};
};
