#if defined(THERMOSTAT_RUN_TESTS)
#include <cstring>

#include "controller/panic_breadcrumb.h"
#include "test_harness.h"

using thermostat::PanicBreadcrumb;

TEST_CASE(panic_breadcrumb_absent_formats_none) {
  PanicBreadcrumb b{};  // all zero: magic invalid
  ASSERT_TRUE(!thermostat::panic_breadcrumb_present(b));
  char out[64];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "none");
}

TEST_CASE(panic_breadcrumb_present_requires_magic_and_pc) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0;  // no pc -> not present
  ASSERT_TRUE(!thermostat::panic_breadcrumb_present(b));
  b.pc = 0x400d1234;
  ASSERT_TRUE(thermostat::panic_breadcrumb_present(b));
}

TEST_CASE(panic_breadcrumb_formats_pc_core_and_backtrace) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.core = 1;
  b.pc = 0x400d1234;
  b.depth = 2;
  b.backtrace[0] = 0x400d5678;
  b.backtrace[1] = 0x400d9abc;
  char out[128];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core1 pc=0x400d1234 bt=0x400d5678,0x400d9abc");
}

TEST_CASE(panic_breadcrumb_depth_is_clamped) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x1;
  b.depth = 999;  // larger than kPanicBacktraceDepth
  for (size_t i = 0; i < thermostat::kPanicBacktraceDepth; ++i) b.backtrace[i] = 0x10 + i;
  char out[256];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  int commas = 0;
  for (const char *p = out; *p; ++p) if (*p == ',') commas++;
  ASSERT_EQ(commas, static_cast<int>(thermostat::kPanicBacktraceDepth) - 1);
}
#endif  // THERMOSTAT_RUN_TESTS
