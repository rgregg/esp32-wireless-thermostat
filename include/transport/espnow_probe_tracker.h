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
// Threading: on_send_complete() runs in the WiFi task; everything else runs in the task
// that sends frames (the main loop). Every field the callback touches is atomic. A
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
  }

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
  // every issued send as completed and forget any probe.
  void resync() {
    clear();
    sends_completed_.store(sends_issued_.load());
  }

 private:
  static constexpr uint32_t kOpenSpan = 0x7FFFFFFFu;

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
};

}  // namespace thermostat
