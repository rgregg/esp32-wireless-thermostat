#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace thermostat {

class AuditLog {
 public:
  static constexpr size_t kMaxEntries = 100;
  static constexpr size_t kMaxLen = 96;

  using PublishCallback = void (*)(const char *msg);

  void set_publish_callback(PublishCallback cb) { publish_cb_ = cb; }

  void add(uint32_t uptime_ms, const char *fmt, ...) {
    char msg[kMaxLen];
    int prefix_len = snprintf(msg, sizeof(msg), "%lus ",
                              static_cast<unsigned long>(uptime_ms / 1000UL));
    if (prefix_len < 0) prefix_len = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg + prefix_len, sizeof(msg) - static_cast<size_t>(prefix_len),
              fmt, ap);
    va_end(ap);
    msg[kMaxLen - 1] = '\0';

    strncpy(entries_[head_], msg, kMaxLen);
    entries_[head_][kMaxLen - 1] = '\0';
    head_ = (head_ + 1) % kMaxEntries;
    if (count_ < kMaxEntries) ++count_;

    if (publish_cb_) publish_cb_(msg);
  }

  size_t count() const { return count_; }

  // index 0 = newest entry
  const char *entry(size_t i) const {
    if (i >= count_) return "";
    size_t idx = (head_ + kMaxEntries - 1 - i) % kMaxEntries;
    return entries_[idx];
  }

 private:
  char entries_[kMaxEntries][kMaxLen] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  PublishCallback publish_cb_ = nullptr;
};

// Format a MAC address into a buffer. Returns pointer to buf for convenience.
inline const char *format_mac(char *buf, size_t buf_len, const uint8_t *mac) {
  if (!mac) {
    buf[0] = '\0';
    return buf;
  }
  snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

}  // namespace thermostat
