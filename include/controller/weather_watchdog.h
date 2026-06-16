#pragma once

#include <stdint.h>

namespace thermostat {

// Decide whether the weather task's heartbeat is stale enough to consider the task
// wedged (and therefore in need of recovery).
//
// Uses SIGNED arithmetic on the millis() difference. This matters because `now_ms` is
// captured once at the top of the main loop, but `heartbeat_ms` is written concurrently
// by the weather task on the other core. If the loop blocks between capturing `now_ms`
// and calling this (e.g. an MQTT (re)connect that does a connect + a burst of subscribes
// and retained publishes, which can take hundreds of ms to seconds), the weather task
// can update `heartbeat_ms` to a value GREATER than the now-stale `now_ms`. With unsigned
// arithmetic `(uint32_t)(now_ms - heartbeat_ms)` would then underflow to ~4.29e9 and
// spuriously exceed any threshold — falsely declaring the task wedged and rebooting the
// device. Signed arithmetic yields a small NEGATIVE age in that case, correctly treating
// a "future" heartbeat as fresh. It is also millis()-wraparound safe for ages < ~24 days
// (the threshold here is ~30 min).
//
// heartbeat_ms == 0 means the task has not started yet → never wedged.
inline bool weather_heartbeat_wedged(uint32_t now_ms, uint32_t heartbeat_ms,
                                     uint32_t threshold_ms) {
  if (heartbeat_ms == 0) return false;
  return static_cast<int32_t>(now_ms - heartbeat_ms) >
         static_cast<int32_t>(threshold_ms);
}

}  // namespace thermostat
