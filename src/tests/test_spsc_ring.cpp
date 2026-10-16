#if defined(THERMOSTAT_RUN_TESTS)
#include "spsc_ring.h"
#include "test_harness.h"

using thermostat::SpscRing;

TEST_CASE(spsc_ring_pops_in_push_order) {
  SpscRing<int, 4> q;
  int v = 0;
  ASSERT_TRUE(!q.pop(v));
  ASSERT_TRUE(q.push(1));
  ASSERT_TRUE(q.push(2));
  ASSERT_TRUE(q.pop(v));
  ASSERT_EQ(v, 1);
  ASSERT_TRUE(q.pop(v));
  ASSERT_EQ(v, 2);
  ASSERT_TRUE(!q.pop(v));
}

TEST_CASE(spsc_ring_drops_when_full) {
  SpscRing<int, 4> q;
  for (int i = 0; i < 4; ++i) ASSERT_TRUE(q.push(i));
  ASSERT_TRUE(!q.push(99));
  int v = 0;
  ASSERT_TRUE(q.pop(v));
  ASSERT_EQ(v, 0);
  ASSERT_TRUE(q.push(4));  // room again
  for (int expect = 1; expect <= 4; ++expect) {
    ASSERT_TRUE(q.pop(v));
    ASSERT_EQ(v, expect);
  }
  ASSERT_TRUE(!q.pop(v));
}

TEST_CASE(spsc_ring_reuses_slots_many_times) {
  SpscRing<uint32_t, 8> q;
  uint32_t v = 0;
  for (uint32_t i = 0; i < 100000; ++i) {
    ASSERT_TRUE(q.push(i));
    ASSERT_TRUE(q.pop(v));
    ASSERT_EQ(v, i);
  }
}

#endif
