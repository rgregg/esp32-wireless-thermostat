#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

namespace thermostat {

// Fixed-size lock-free queue for exactly one producer task and one consumer task (for
// example the WiFi task's ESP-NOW receive callback and the loop task). push() never
// blocks: it returns false when the queue is full and the item is dropped.
template <typename T, size_t N>
class SpscRing {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

 public:
  // Producer side only.
  bool push(const T &item) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= N) return false;
    items_[head & (N - 1)] = item;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // Consumer side only.
  bool pop(T &item) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    item = items_[tail & (N - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

 private:
  T items_[N]{};
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};

}  // namespace thermostat
