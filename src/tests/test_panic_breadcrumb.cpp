#if defined(THERMOSTAT_RUN_TESTS)
#include <cstring>
#include <string>

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
  ASSERT_TRUE(strncmp(out, "core0 pc=0x00000001 bt=", 23) == 0);
}

TEST_CASE(panic_breadcrumb_format_tiny_buffer_is_safe) {
  // out_len == 1 must never write past the buffer; result is a valid empty C-string.
  PanicBreadcrumb present{};
  present.magic = thermostat::kPanicBreadcrumbMagic;
  present.pc = 0x400d1234;
  char one[1] = {'X'};
  thermostat::panic_breadcrumb_format(present, one, sizeof(one));
  ASSERT_EQ(one[0], '\0');

  // A mid-format truncation must still leave a null-terminated string.
  char small[8] = {0};
  thermostat::panic_breadcrumb_format(present, small, sizeof(small));
  ASSERT_EQ(small[sizeof(small) - 1], '\0');

  // out_len == 0 must not touch the buffer at all.
  char guard[2] = {'A', 'B'};
  thermostat::panic_breadcrumb_format(present, guard, 0);
  ASSERT_EQ(guard[0], 'A');
}

TEST_CASE(panic_breadcrumb_stale_magic_is_not_present) {
  // The v1 layout had no reason/flags fields; a pre-upgrade record must be
  // rejected so we never format garbage from the old struct tail.
  PanicBreadcrumb b{};
  b.magic = 0x504e4943u;  // kPanicBreadcrumbMagic v1
  b.pc = 0x400d1234;
  ASSERT_TRUE(!thermostat::panic_breadcrumb_present(b));
}

TEST_CASE(panic_breadcrumb_formats_reason) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.core = 1;
  b.pc = 0x400d1234;
  b.depth = 1;
  b.backtrace[0] = 0x400d5678;
  std::strcpy(b.reason, "LoadProhibited");
  char out[192];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core1 pc=0x400d1234 rsn=LoadProhibited bt=0x400d5678");
}

TEST_CASE(panic_breadcrumb_omits_empty_reason) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x400d1234;
  b.depth = 1;
  b.backtrace[0] = 0x400d5678;
  b.reason[0] = '\0';
  char out[192];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core0 pc=0x400d1234 bt=0x400d5678");
}

TEST_CASE(panic_breadcrumb_reason_unterminated_is_bounded) {
  // RTC memory can hold garbage: a reason with no NUL must not over-read.
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x1;
  b.depth = 0;
  for (size_t i = 0; i < thermostat::kPanicReasonLen; ++i) b.reason[i] = 'A';
  char out[192];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  const std::string expected =
      "core0 pc=0x00000001 rsn=" + std::string(thermostat::kPanicReasonLen - 1, 'A');
  ASSERT_STR_EQ(out, expected.c_str());
}

TEST_CASE(panic_breadcrumb_reason_sanitizes_non_printable) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x1;
  b.depth = 0;
  b.reason[0] = 'A';
  b.reason[1] = '\n';
  b.reason[2] = static_cast<char>(0x7f);
  b.reason[3] = 'B';
  b.reason[4] = '\0';
  char out[192];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core0 pc=0x00000001 rsn=A??B");
}

TEST_CASE(panic_breadcrumb_formats_flags) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x1;
  b.depth = 1;
  b.backtrace[0] = 0x2;
  b.flags = thermostat::kPanicFlagBacktraceCorrupt;
  char out[192];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core0 pc=0x00000001 bt=0x00000002 +corrupt");

  b.flags = thermostat::kPanicFlagBacktraceContinues;
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core0 pc=0x00000001 bt=0x00000002 +more");
}

TEST_CASE(panic_breadcrumb_worst_case_fits_reporting_buffer) {
  // The firmware formats into a fixed stack buffer; prove the widest possible
  // record still round-trips without truncation.
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.core = 1;
  b.pc = 0xffffffffu;
  b.depth = thermostat::kPanicBacktraceDepth;
  for (size_t i = 0; i < thermostat::kPanicBacktraceDepth; ++i) b.backtrace[i] = 0xffffffffu;
  for (size_t i = 0; i < thermostat::kPanicReasonLen - 1; ++i) b.reason[i] = 'W';
  b.reason[thermostat::kPanicReasonLen - 1] = '\0';
  b.flags = thermostat::kPanicFlagBacktraceCorrupt | thermostat::kPanicFlagBacktraceContinues;
  char out[thermostat::kPanicBreadcrumbTextMax];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  // Not truncated: both flag suffixes survived at the very end.
  const std::string s(out);
  ASSERT_TRUE(s.find("+corrupt") != std::string::npos);
  ASSERT_TRUE(s.find("+more") != std::string::npos);
  ASSERT_TRUE(s.size() < thermostat::kPanicBreadcrumbTextMax);
}
#endif  // THERMOSTAT_RUN_TESTS
