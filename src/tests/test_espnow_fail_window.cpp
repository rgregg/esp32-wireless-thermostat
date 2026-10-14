#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "transport/espnow_fail_window.h"

using thermostat::RecentFailWindow;

TEST_CASE(fail_window_none_when_counter_never_moves) {
  RecentFailWindow w;
  for (uint32_t t = 0; t < 1000000; t += 5000) {
    w.update(t, 0);
    ASSERT_TRUE(!w.recent(t));
  }
}

TEST_CASE(fail_window_reports_failure_for_5_minutes_then_clears) {
  RecentFailWindow w;
  w.update(1000, 0);
  w.update(6000, 1);  // one failure
  ASSERT_TRUE(w.recent(6000));
  w.update(300000, 1);
  ASSERT_TRUE(w.recent(300000));    // 294 s later: still inside the window
  w.update(306000, 1);
  ASSERT_TRUE(!w.recent(306000));   // 300 s after the failure: cleared
}

TEST_CASE(fail_window_new_failure_extends_window) {
  RecentFailWindow w;
  w.update(1000, 1);
  w.update(200000, 2);
  ASSERT_TRUE(w.recent(450000));    // 250 s after the second failure
  ASSERT_TRUE(!w.recent(500001));
}

TEST_CASE(fail_window_survives_millis_wraparound) {
  RecentFailWindow w;
  w.update(0xFFFFFF00u, 1);
  ASSERT_TRUE(w.recent(0x00000100u));  // 512 ms later, across the wrap
}

#endif
