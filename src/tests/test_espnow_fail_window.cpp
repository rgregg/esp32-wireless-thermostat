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
  w.update(1000, 0);  // seeds baseline
  w.update(6000, 1);  // one failure
  ASSERT_TRUE(w.recent(6000));
  w.update(300000, 1);
  ASSERT_TRUE(w.recent(300000));    // 294 s later: still inside the window
  w.update(306000, 1);
  ASSERT_TRUE(!w.recent(306000));   // 300 s after the failure: cleared
}

TEST_CASE(fail_window_new_failure_extends_window) {
  RecentFailWindow w;
  w.update(0, 0);     // seeds baseline
  w.update(1000, 1);  // first failure
  w.update(200000, 2);
  ASSERT_TRUE(w.recent(450000));    // 250 s after the second failure
  ASSERT_TRUE(!w.recent(500001));
}

TEST_CASE(fail_window_survives_millis_wraparound) {
  RecentFailWindow w;
  w.update(0xFFFFFF00u, 0);  // seeds baseline
  w.update(0xFFFFFF00u, 1);  // failure just before the wrap
  ASSERT_TRUE(w.recent(0x00000100u));  // 512 ms later, across the wrap
}

TEST_CASE(fail_window_first_nonzero_count_is_not_a_failure) {
  RecentFailWindow w;
  w.update(1000, 5);  // first observation seeds the baseline only
  ASSERT_TRUE(!w.recent(1000));
  ASSERT_TRUE(!w.recent(2000));
}

TEST_CASE(fail_window_counter_reset_is_not_a_failure) {
  RecentFailWindow w;
  w.update(0, 5);     // seeds baseline at 5
  w.update(1000, 2);  // counter went backwards (transport restart)
  ASSERT_TRUE(!w.recent(1000));
  w.update(2000, 3);  // increase from the new baseline (2) is a real failure
  ASSERT_TRUE(w.recent(2000));
}

TEST_CASE(fail_window_huge_jump_only_rebaselines) {
  RecentFailWindow w;
  w.update(0, 0);                      // seeds baseline
  w.update(1000, 0x80000000u);         // delta >= 0x80000000: re-baseline only
  ASSERT_TRUE(!w.recent(1000));
  w.update(2000, 0x80000001u);         // delta 1 from the new baseline
  ASSERT_TRUE(w.recent(2000));
}

TEST_CASE(fail_window_exact_expiry_edge) {
  RecentFailWindow w;
  w.update(0, 0);      // seeds baseline
  w.update(1000, 1);   // failure at t = 1000
  ASSERT_TRUE(w.recent(1000 + 299999));
  ASSERT_TRUE(!w.recent(1000 + 300000));
}

#endif
