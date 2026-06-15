#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace thermostat {

constexpr uint32_t kPanicBreadcrumbMagic = 0x504e4943u;
constexpr size_t kPanicBacktraceDepth = 8;

struct PanicBreadcrumb {
  uint32_t magic;
  uint32_t core;
  uint32_t pc;
  uint32_t depth;
  uint32_t backtrace[kPanicBacktraceDepth];
};

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
  for (size_t i = 0; i < depth; ++i) {
    const char *sep = (i == 0) ? " bt=" : ",";
    int m = snprintf(out + pos, out_len - pos, "%s0x%08lx", sep,
                     static_cast<unsigned long>(b.backtrace[i]));
    if (m < 0 || static_cast<size_t>(m) >= out_len - pos) return;
    pos += static_cast<size_t>(m);
  }
}

}  // namespace thermostat
