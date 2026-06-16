#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/weather_watchdog.h"
#include "test_harness.h"

using thermostat::weather_heartbeat_wedged;

// ~30 min, matching the firmware's kCtrlWeatherTaskWatchdogMs (2*15min + 2*4s).
static const uint32_t kThreshold = 2u * 15u * 60u * 1000u + 2u * 4000u;  // 1,808,000

TEST_CASE(weather_wd_fresh_not_wedged) {
  // now == heartbeat -> age 0 -> not wedged.
  ASSERT_TRUE(!weather_heartbeat_wedged(500000u, 500000u, kThreshold));
}

TEST_CASE(weather_wd_below_threshold_not_wedged) {
  // 10 min stale, threshold ~30 min -> not wedged.
  ASSERT_TRUE(!weather_heartbeat_wedged(1100000u, 500000u, kThreshold));
}

TEST_CASE(weather_wd_heartbeat_ahead_of_now_not_wedged) {
  // THE BUG: heartbeat updated concurrently to a value GREATER than the stale now_ms.
  // Unsigned math would underflow to ~4.29e9 and falsely report wedged; signed math
  // yields a small negative age -> fresh.
  ASSERT_TRUE(!weather_heartbeat_wedged(730000u, 733500u, kThreshold));  // 3.5s ahead
  ASSERT_TRUE(!weather_heartbeat_wedged(730000u, 730001u, kThreshold));  // 1ms ahead
}

TEST_CASE(weather_wd_real_stale_is_wedged) {
  // Genuinely stale: heartbeat frozen far in the past -> wedged.
  ASSERT_TRUE(weather_heartbeat_wedged(2'000'000u, 100'000u, kThreshold));  // ~31.7 min old
}

TEST_CASE(weather_wd_exact_threshold_not_wedged) {
  // Strictly greater-than: age == threshold is NOT yet wedged.
  ASSERT_TRUE(!weather_heartbeat_wedged(100000u + kThreshold, 100000u, kThreshold));
  // One ms past threshold -> wedged.
  ASSERT_TRUE(weather_heartbeat_wedged(100001u + kThreshold, 100000u, kThreshold));
}

TEST_CASE(weather_wd_zero_heartbeat_not_wedged) {
  // Task not started yet (heartbeat 0) -> never wedged, even at large now.
  ASSERT_TRUE(!weather_heartbeat_wedged(5'000'000u, 0u, kThreshold));
}

TEST_CASE(weather_wd_millis_wraparound_real_stale) {
  // now_ms wrapped past 0; heartbeat was just before the wrap. Real age slightly over
  // threshold -> wedged, handled by signed arithmetic.
  const uint32_t hb = 0xFFFFFFFFu - 100000u;        // 100s before wrap
  const uint32_t now = hb + kThreshold + 5000u;     // ~5s past threshold (wraps through 0)
  ASSERT_TRUE(weather_heartbeat_wedged(now, hb, kThreshold));
}

TEST_CASE(weather_wd_millis_wraparound_fresh) {
  // Same wrap region but well within threshold -> not wedged.
  const uint32_t hb = 0xFFFFFFFFu - 100000u;
  const uint32_t now = hb + 60000u;  // 60s later, wraps through 0
  ASSERT_TRUE(!weather_heartbeat_wedged(now, hb, kThreshold));
}
#endif  // THERMOSTAT_RUN_TESTS
