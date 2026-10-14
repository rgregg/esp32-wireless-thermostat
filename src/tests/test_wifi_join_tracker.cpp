#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "wifi_join_tracker.h"

TEST_CASE(wifi_join_idle_until_begin) {
  WifiJoinTracker j;
  ASSERT_TRUE(!j.in_progress(0));
  ASSERT_TRUE(!j.in_progress(123456));
}

TEST_CASE(wifi_join_ends_on_got_ip) {
  WifiJoinTracker j;
  j.on_begin(1000);
  ASSERT_TRUE(j.in_progress(1000));
  ASSERT_TRUE(j.in_progress(5000));
  j.on_got_ip();
  ASSERT_TRUE(!j.in_progress(5001));
}

TEST_CASE(wifi_join_ends_on_failure_disconnect) {
  WifiJoinTracker j;
  j.on_begin(1000);
  j.on_disconnected(201);  // WIFI_REASON_NO_AP_FOUND
  ASSERT_TRUE(!j.in_progress(1500));
}

TEST_CASE(wifi_join_ignores_local_leave_disconnect) {
  // WiFi.begin() can tear down the previous attempt first; that ASSOC_LEAVE (8) event
  // is not the outcome of the new join.
  WifiJoinTracker j;
  j.on_begin(1000);
  j.on_disconnected(8);
  ASSERT_TRUE(j.in_progress(1500));
}

TEST_CASE(wifi_join_capped_at_10s) {
  WifiJoinTracker j;
  j.on_begin(1000);
  ASSERT_TRUE(j.in_progress(10999));
  ASSERT_TRUE(!j.in_progress(11000));
}

TEST_CASE(wifi_join_cancel_and_restart) {
  WifiJoinTracker j;
  j.on_begin(1000);
  j.cancel();
  ASSERT_TRUE(!j.in_progress(1001));
  j.on_begin(20000);
  ASSERT_TRUE(j.in_progress(20001));
}

TEST_CASE(wifi_join_cap_survives_millis_wraparound) {
  WifiJoinTracker j;
  j.on_begin(0xFFFFF000u);
  ASSERT_TRUE(j.in_progress(0x00000100u));   // ~4.3 s later, across the wrap
  ASSERT_TRUE(!j.in_progress(0x00002000u));  // ~12 s later
}

#endif
