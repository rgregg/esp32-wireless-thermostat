#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "wifi_join_tracker.h"

namespace {
constexpr uint8_t kNoApFound = 201;
constexpr uint8_t kAuthFail = 202;
constexpr uint8_t kBeaconTimeout = 200;
constexpr uint8_t kAssocLeave = 8;

bool start_ok() { return true; }
bool start_fails() { return false; }

// Use up arduino-esp32's one automatic retry so that a failure disconnect ends the join.
void use_framework_retry(WifiJoinTracker &j) {
  j.on_disconnected(kNoApFound, 0);
  j.cancel();
}
}  // namespace

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
  use_framework_retry(j);
  j.on_begin(1000);
  j.on_disconnected(kNoApFound, 1400);
  ASSERT_TRUE(!j.in_progress(1500));
}

TEST_CASE(wifi_join_ignores_local_leave_disconnect) {
  // WiFi.begin() can tear down the previous attempt first; that ASSOC_LEAVE (8) event
  // is not the outcome of the new join.
  WifiJoinTracker j;
  use_framework_retry(j);
  j.on_begin(1000);
  j.on_disconnected(kAssocLeave, 1200);
  ASSERT_TRUE(j.in_progress(1500));
}

TEST_CASE(wifi_join_first_failure_since_boot_is_the_framework_retry) {
  // arduino-esp32 reconnects by itself once after the first non-LEAVE disconnect since
  // boot, so that disconnect starts a new join instead of ending one.
  WifiJoinTracker j;
  j.on_begin(1000);
  j.on_disconnected(kAssocLeave, 1100);  // a local leave does not use up the retry
  ASSERT_TRUE(j.in_progress(1200));
  j.on_disconnected(kAuthFail, 3000);    // first failure: the framework retries
  ASSERT_TRUE(j.in_progress(12999));     // capped from the retry, not from the begin
  ASSERT_TRUE(!j.in_progress(13000));
  j.on_begin(20000);
  j.on_disconnected(kAuthFail, 21000);   // later failures end the join
  ASSERT_TRUE(!j.in_progress(21001));
}

TEST_CASE(wifi_join_first_disconnect_after_connection_is_also_a_retry) {
  // The framework retry also follows losing an established connection.
  WifiJoinTracker j;
  j.on_begin(1000);
  j.on_got_ip();
  j.on_disconnected(kBeaconTimeout, 60000);  // first non-LEAVE disconnect since boot
  ASSERT_TRUE(j.in_progress(60001));
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

TEST_CASE(wifi_join_try_begin_starts_a_join) {
  WifiJoinTracker j;
  ASSERT_TRUE(j.try_begin(1000, start_ok) == WifiJoinTracker::Start::Started);
  ASSERT_TRUE(j.in_progress(1001));
}

TEST_CASE(wifi_join_try_begin_cancels_when_connect_did_not_start) {
  // e.g. empty or over-long SSID: the framework never calls esp_wifi_connect, so no
  // event would ever end the join.
  WifiJoinTracker j;
  ASSERT_TRUE(j.try_begin(1000, start_fails) == WifiJoinTracker::Start::Failed);
  ASSERT_TRUE(!j.in_progress(1001));
  // Repeated failed starts never accumulate into a stuck join.
  ASSERT_TRUE(j.try_begin(6000, start_fails) == WifiJoinTracker::Start::Failed);
  ASSERT_TRUE(!j.in_progress(6001));
}

TEST_CASE(wifi_join_try_begin_refuses_while_join_in_progress) {
  WifiJoinTracker j;
  int starts = 0;
  auto start = [&starts]() {
    ++starts;
    return true;
  };
  ASSERT_TRUE(j.try_begin(1000, start) == WifiJoinTracker::Start::Started);
  ASSERT_TRUE(j.try_begin(6000, start) == WifiJoinTracker::Start::Busy);
  ASSERT_TRUE(starts == 1);           // no overlapping attempt
  ASSERT_TRUE(j.in_progress(10999));  // and the running join's cap was not re-stamped
  ASSERT_TRUE(!j.in_progress(11000));
  ASSERT_TRUE(j.try_begin(11000, start) == WifiJoinTracker::Start::Started);  // backstop
  ASSERT_TRUE(starts == 2);
}

// --- Associated at L2 but no IP (AP up, DHCP/gateway down) ---

TEST_CASE(wifi_join_associated_without_ip_ends_the_join_at_20s) {
  WifiJoinTracker j;
  ASSERT_TRUE(j.try_begin(1000, start_ok) == WifiJoinTracker::Start::Started);
  // Associates 3 s in; the 10 s cap no longer applies, the 20 s no-IP window does.
  ASSERT_TRUE(j.on_no_ip(4000, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.in_progress(11000));
  ASSERT_TRUE(j.on_no_ip(23999, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.in_progress(23999));
  ASSERT_TRUE(!j.in_progress(24000));
  ASSERT_TRUE(j.on_no_ip(24000, true) == WifiJoinTracker::NoIp::TimedOut);
  ASSERT_TRUE(!j.in_progress(24001));
  // The caller's disconnect is a local leave; it must not start or revive a join.
  j.on_disconnected(kAssocLeave, 24050);
  ASSERT_TRUE(j.on_no_ip(24100, false) == WifiJoinTracker::NoIp::NotAssociated);
  ASSERT_TRUE(!j.in_progress(24100));
}

TEST_CASE(wifi_join_no_rebegin_while_associated_inside_the_window) {
  WifiJoinTracker j;
  int starts = 0;
  auto start = [&starts]() {
    ++starts;
    return true;
  };
  ASSERT_TRUE(j.try_begin(1000, start) == WifiJoinTracker::Start::Started);
  ASSERT_TRUE(j.on_no_ip(2000, true) == WifiJoinTracker::NoIp::Waiting);
  for (uint32_t t = 2000; t < 22000; t += 500) {
    ASSERT_TRUE(j.on_no_ip(t, true) == WifiJoinTracker::NoIp::Waiting);
    ASSERT_TRUE(j.try_begin(t, start) == WifiJoinTracker::Start::Busy);
  }
  ASSERT_EQ(starts, 1);
  ASSERT_TRUE(j.on_no_ip(22000, true) == WifiJoinTracker::NoIp::TimedOut);
  // Disconnected: the next attempt may start (after the caller's retry interval).
  ASSERT_TRUE(j.on_no_ip(27000, false) == WifiJoinTracker::NoIp::NotAssociated);
  ASSERT_TRUE(j.try_begin(27000, start) == WifiJoinTracker::Start::Started);
  ASSERT_EQ(starts, 2);
}

TEST_CASE(wifi_join_association_outside_a_join_still_gets_the_window) {
  // e.g. the IP lease is lost while the STA stays associated, or a join that hit its
  // 10 s cap associates late: no connect may be issued until the window expires.
  WifiJoinTracker j;
  ASSERT_TRUE(j.on_no_ip(50000, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.try_begin(55000, start_ok) == WifiJoinTracker::Start::Busy);
  ASSERT_TRUE(j.on_no_ip(70000, true) == WifiJoinTracker::NoIp::TimedOut);
}

TEST_CASE(wifi_join_got_ip_ends_associated_join) {
  WifiJoinTracker j;
  ASSERT_TRUE(j.try_begin(1000, start_ok) == WifiJoinTracker::Start::Started);
  ASSERT_TRUE(j.on_no_ip(2000, true) == WifiJoinTracker::NoIp::Waiting);
  j.on_got_ip();
  ASSERT_TRUE(!j.in_progress(2001));
  // Losing the IP later while still associated starts a fresh window.
  ASSERT_TRUE(j.on_no_ip(600000, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.on_no_ip(619999, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.on_no_ip(620000, true) == WifiJoinTracker::NoIp::TimedOut);
}

TEST_CASE(wifi_join_disconnect_clears_association) {
  WifiJoinTracker j;
  use_framework_retry(j);
  ASSERT_TRUE(j.try_begin(1000, start_ok) == WifiJoinTracker::Start::Started);
  ASSERT_TRUE(j.on_no_ip(2000, true) == WifiJoinTracker::NoIp::Waiting);
  j.on_disconnected(kBeaconTimeout, 5000);  // AP lost: the join failed
  ASSERT_TRUE(!j.in_progress(5001));
  ASSERT_TRUE(j.try_begin(10000, start_ok) == WifiJoinTracker::Start::Started);
  // A new association gets a full window of its own.
  ASSERT_TRUE(j.on_no_ip(12000, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.on_no_ip(31999, true) == WifiJoinTracker::NoIp::Waiting);
  ASSERT_TRUE(j.on_no_ip(32000, true) == WifiJoinTracker::NoIp::TimedOut);
}

#endif
