#pragma once

#include <stddef.h>
#include <stdint.h>

#include <unordered_map>

namespace thermostat {

// Decides whether a retained MQTT state publish carries anything new. A retained topic
// already holds its last value at the broker, so re-sending an unchanged value only
// costs broker traffic and Home Assistant recorder rows.
//
// should_publish() returns true, and records the send, when the topic has not been sent
// since the last reset(), or when its payload changed and at least min_interval_ms has
// passed since the topic was last sent (0 = send every change). Callers reset() on every
// MQTT (re)connect and periodically, so a broker that lost its retained state is
// refilled. Topics and payloads are stored as 32-bit FNV-1a hashes.
//
// Not thread-safe: use it from one task (or under the lock that guards publishing).
class MqttPublishDedup {
 public:
  bool should_publish(const char *topic, const char *payload, uint32_t now_ms,
                      uint32_t min_interval_ms = 0) {
    const uint32_t key = fnv1a(topic);
    const uint32_t value = fnv1a(payload);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
      entries_[key] = Entry{value, now_ms};
      return true;
    }
    Entry &e = it->second;
    if (e.value == value) return false;
    if (min_interval_ms != 0 && static_cast<uint32_t>(now_ms - e.sent_ms) < min_interval_ms) {
      return false;
    }
    e.value = value;
    e.sent_ms = now_ms;
    return true;
  }

  // Call when the publish should_publish() allowed did not go out, so the next call
  // sends the topic again.
  void forget(const char *topic) { entries_.erase(fnv1a(topic)); }

  void reset() { entries_.clear(); }
  size_t size() const { return entries_.size(); }

  static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    if (s == nullptr) return h;
    for (; *s != '\0'; ++s) {
      h ^= static_cast<uint8_t>(*s);
      h *= 16777619u;
    }
    return h;
  }

 private:
  struct Entry {
    uint32_t value;
    uint32_t sent_ms;
  };
  std::unordered_map<uint32_t, Entry> entries_;
};

}  // namespace thermostat
