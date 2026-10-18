#pragma once

#include <stdint.h>

#include <atomic>

namespace thermostat {

// Outcome of the most recent ESP-NOW channel probe sent by a transport's send_probe().
//   None    - no probe sent since the last clear_probe()
//   Pending - at least one probe frame still awaits its send callback
//   Acked   - every probe frame has completed and at least one was acknowledged
//   Failed  - every probe frame has completed and none was acknowledged
enum class ProbeStatus : uint8_t { None = 0, Pending = 1, Acked = 2, Failed = 3 };

// Attributes ESP-NOW send callbacks to the probe that caused them. ESP-NOW delivers send
// callbacks in send order, so callback number N belongs to accepted send number N. The
// transport calls on_send_accepted() after every esp_now_send that returns ESP_OK and
// on_send_complete() once per send callback.
//
// Threading: on_send_complete() runs in the WiFi task; everything else (including
// check_lost_callbacks()) runs in the task that sends frames (the main loop). Every field the callback touches is atomic. A
// callback delivered before the sender has counted its send is still attributed
// correctly, because the probe's range stays open-ended until end_probe().
class EspNowProbeTracker {
 public:
  void on_send_accepted() { sends_issued_.fetch_add(1); }

  void on_send_complete(bool ok) {
    const uint32_t n = sends_completed_.fetch_add(1) + 1;
    if (ok && probe_pending_.load() && in_probe_range(n)) {
      probe_ack_seq_.store(n);
    }
    // One CAS on (send number, state): a frame tracked after this callback started can
    // never be given its outcome.
    uint32_t expected = pack_tracked(n, kTrackPending);
    tracked_.compare_exchange_strong(expected,
                                     pack_tracked(n, ok ? kTrackDelivered : kTrackFailed));
  }

  // Delivery tracking for one frame at a time (same send-order attribution as probes).
  // Call begin_tracked() just before sending the frame and end_tracked() after; a newer
  // tracked frame replaces the previous one.
  void begin_tracked() { tracked_.store(pack_tracked(sends_issued_.load() + 1, kTrackPending)); }
  // attempted: the frame was handed to esp_now_send (not held back by a send throttle).
  // A frame esp_now_send rejected gets no callback, so it counts as failed; one never
  // attempted is not tracked.
  void end_tracked(bool attempted) {
    uint32_t cur = tracked_.load();
    if ((cur & kTrackStateMask) != kTrackPending) return;
    const uint32_t first = cur >> 2;
    if (((sends_issued_.load() + 1) & kTrackNumMask) != first) return;  // was accepted
    tracked_.compare_exchange_strong(cur, attempted ? (cur & ~kTrackStateMask) | kTrackFailed
                                                    : 0u);
  }
  // kTrackDelivered or kTrackFailed once, then kTrackNone; kTrackPending while waiting.
  uint8_t take_tracked() {
    uint32_t cur = tracked_.load();
    for (;;) {
      const uint8_t state = static_cast<uint8_t>(cur & kTrackStateMask);
      if (state != kTrackDelivered && state != kTrackFailed) return state;
      if (tracked_.compare_exchange_weak(cur, 0u)) return state;
    }
  }
  static constexpr uint8_t kTrackNone = 0, kTrackPending = 1, kTrackDelivered = 2,
                           kTrackFailed = 3;

  // Call immediately before sending the probe's frames.
  void begin_probe() {
    probe_pending_.store(false);
    // No send of ours is outstanding here, so completions ahead of issued sends can only
    // be stray callbacks; drop them so they cannot complete this probe.
    const uint32_t issued = sends_issued_.load();
    if (static_cast<int32_t>(sends_completed_.load() - issued) > 0) {
      sends_completed_.store(issued);
    }
    const uint32_t first = sends_issued_.load() + 1;
    probe_first_seq_.store(first);
    probe_last_seq_.store(first + kOpenSpan);  // open until end_probe()
    probe_ack_seq_.store(first - 1);           // outside the range: not acked
    probe_pending_.store(true);
  }

  // Call after the probe's frames were sent. Returns false (and clears the probe) when
  // none of them was accepted.
  bool end_probe() {
    const uint32_t last = sends_issued_.load();
    if (last == probe_first_seq_.load() - 1) {
      clear();
      return false;
    }
    probe_last_seq_.store(last);
    return true;
  }

  ProbeStatus status() const {
    if (!probe_pending_.load()) return ProbeStatus::None;
    const uint32_t last = probe_last_seq_.load();
    if (static_cast<int32_t>(last - sends_completed_.load()) > 0) return ProbeStatus::Pending;
    return in_probe_range(probe_ack_seq_.load()) ? ProbeStatus::Acked : ProbeStatus::Failed;
  }

  void clear() { probe_pending_.store(false); }

  // After (re)initialising ESP-NOW: esp_now_deinit can drop pending callbacks, so treat
  // every issued send as completed and forget any probe. A tracked frame still waiting
  // may have lost its callback: report it failed so it is re-sent.
  void resync() {
    clear();
    sends_completed_.store(sends_issued_.load());
    lagging_ = false;
    uint32_t cur = tracked_.load();
    if ((cur & kTrackStateMask) == kTrackPending) {
      tracked_.compare_exchange_strong(cur, (cur & ~kTrackStateMask) | kTrackFailed);
    }
  }

  // Lost-callback guard; call regularly from the sending task. A send callback that never
  // arrives would leave every later callback attributed one send too early, so a probe's
  // own callback would never complete it. If no probe is outstanding and completions have
  // stayed behind issued sends for more than kLostCallbackMs (callbacks normally arrive
  // within milliseconds), resync(). Time with a probe outstanding does not count.
  void check_lost_callbacks(uint32_t now_ms) {
    const bool behind =
        static_cast<int32_t>(sends_issued_.load() - sends_completed_.load()) > 0;
    if (!behind || probe_pending_.load()) {
      lagging_ = false;
      return;
    }
    if (!lagging_) {
      lagging_ = true;
      lag_since_ms_ = now_ms;
      return;
    }
    if (static_cast<uint32_t>(now_ms - lag_since_ms_) > kLostCallbackMs) resync();
  }

 private:
  static constexpr uint32_t kOpenSpan = 0x7FFFFFFFu;
  static constexpr uint32_t kLostCallbackMs = 1000;
  static constexpr uint32_t kTrackStateMask = 0x3u;
  static constexpr uint32_t kTrackNumMask = 0x3FFFFFFFu;

  // Tracked frame word: (send number mod 2^30) << 2 | state. kTrackNone is always stored
  // as 0, so number and state change together in one atomic.
  static uint32_t pack_tracked(uint32_t n, uint8_t state) {
    return ((n & kTrackNumMask) << 2) | state;
  }

  // Wraparound-safe first <= n <= last.
  bool in_probe_range(uint32_t n) const {
    const uint32_t first = probe_first_seq_.load();
    return static_cast<uint32_t>(n - first) <=
           static_cast<uint32_t>(probe_last_seq_.load() - first);
  }

  std::atomic<uint32_t> sends_issued_{0};
  std::atomic<uint32_t> sends_completed_{0};
  std::atomic<bool> probe_pending_{false};
  std::atomic<uint32_t> probe_first_seq_{0};
  std::atomic<uint32_t> probe_last_seq_{0};
  // Sequence number of an acknowledged probe frame. Acked iff it lies inside the current
  // probe's range, so a late write for an earlier probe can never ack a later one.
  std::atomic<uint32_t> probe_ack_seq_{0};
  std::atomic<uint32_t> tracked_{0};
  // Lost-callback guard state; touched only by the sending task.
  bool lagging_ = false;
  uint32_t lag_since_ms_ = 0;
};

}  // namespace thermostat
