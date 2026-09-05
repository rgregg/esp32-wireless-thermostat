#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace thermostat {

// Bumped from 'PNIC' (v1, no reason/flags fields) so a breadcrumb written by
// pre-upgrade firmware and still sitting in RTC_NOINIT after an OTA is rejected
// rather than formatted with garbage read from the new struct tail.
constexpr uint32_t kPanicBreadcrumbMagic = 0x504e4932u;  // 'PNI2'
constexpr size_t kPanicBacktraceDepth = 8;
// Longest IDF reason we care to keep intact is "Cache disabled but cached
// memory region accessed"; 32 truncates it to a still-unambiguous prefix.
constexpr size_t kPanicReasonLen = 32;

constexpr uint32_t kPanicFlagBacktraceCorrupt = 1u << 0;
constexpr uint32_t kPanicFlagBacktraceContinues = 1u << 1;

struct PanicBreadcrumb {
  uint32_t magic;
  uint32_t core;
  uint32_t pc;
  uint32_t depth;
  uint32_t flags;
  uint32_t backtrace[kPanicBacktraceDepth];
  char reason[kPanicReasonLen];
};

// Widest possible panic_breadcrumb_format() output, including the NUL.
constexpr size_t kPanicBreadcrumbTextMax =
    // "coreN pc=0xXXXXXXXX"
    5 + 10 + 4 + 10 +
    // " rsn=" + reason
    5 + (kPanicReasonLen - 1) +
    // " bt=" + depth * "0xXXXXXXXX" + (depth-1) separators
    4 + kPanicBacktraceDepth * 11 +
    // " +corrupt" " +more"
    9 + 6 + 1;

inline bool panic_breadcrumb_present(const PanicBreadcrumb &b) {
  return b.magic == kPanicBreadcrumbMagic && b.pc != 0;
}

inline void panic_breadcrumb_format(const PanicBreadcrumb &b, char *out, size_t out_len) {
  if (out == nullptr || out_len == 0) return;
  if (!panic_breadcrumb_present(b)) {
    snprintf(out, out_len, "none");
    return;
  }
  size_t depth = b.depth;
  if (depth > kPanicBacktraceDepth) depth = kPanicBacktraceDepth;
  int n = snprintf(out, out_len, "core%lu pc=0x%08lx",
                   static_cast<unsigned long>(b.core),
                   static_cast<unsigned long>(b.pc));
  if (n < 0 || static_cast<size_t>(n) >= out_len) return;
  size_t pos = static_cast<size_t>(n);

  // The reason is copied from a flash-resident literal in panic context and may
  // be truncated, unterminated, or (on a stale/garbled record) binary. Bound the
  // scan to the field and sanitize so this can never emit control characters
  // into an MQTT payload.
  if (b.reason[0] != '\0') {
    char safe[kPanicReasonLen];
    size_t len = 0;
    while (len + 1 < sizeof(safe) && b.reason[len] != '\0') {
      const unsigned char c = static_cast<unsigned char>(b.reason[len]);
      safe[len] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
      ++len;
    }
    safe[len] = '\0';
    int m = snprintf(out + pos, out_len - pos, " rsn=%s", safe);
    if (m < 0 || static_cast<size_t>(m) >= out_len - pos) return;
    pos += static_cast<size_t>(m);
  }

  for (size_t i = 0; i < depth; ++i) {
    const char *sep = (i == 0) ? " bt=" : ",";
    int m = snprintf(out + pos, out_len - pos, "%s0x%08lx", sep,
                     static_cast<unsigned long>(b.backtrace[i]));
    if (m < 0 || static_cast<size_t>(m) >= out_len - pos) return;
    pos += static_cast<size_t>(m);
  }

  // The panic handler stops unwinding on a bad frame, so "+corrupt" is the
  // signal that the 1-deep backtrace is a truncation, not the real depth.
  if ((b.flags & kPanicFlagBacktraceCorrupt) != 0) {
    int m = snprintf(out + pos, out_len - pos, " +corrupt");
    if (m < 0 || static_cast<size_t>(m) >= out_len - pos) return;
    pos += static_cast<size_t>(m);
  }
  if ((b.flags & kPanicFlagBacktraceContinues) != 0) {
    snprintf(out + pos, out_len - pos, " +more");
  }
}

}  // namespace thermostat
