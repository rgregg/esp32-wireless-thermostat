#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "mqtt_publish_dedup.h"

using thermostat::MqttPublishDedup;

TEST_CASE(publish_dedup_sends_first_value_then_skips_repeats) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("dev/state/mode", "heat", 0));
  ASSERT_TRUE(!d.should_publish("dev/state/mode", "heat", 10000));
  ASSERT_TRUE(!d.should_publish("dev/state/mode", "heat", 600000));
}

TEST_CASE(publish_dedup_sends_every_change_without_interval) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("dev/state/mode", "heat", 0));
  ASSERT_TRUE(d.should_publish("dev/state/mode", "cool", 5));
  ASSERT_TRUE(d.should_publish("dev/state/mode", "heat", 6));
}

TEST_CASE(publish_dedup_topics_are_independent) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("dev/state/mode", "heat", 0));
  ASSERT_TRUE(d.should_publish("dev/state/fan_mode", "heat", 0));
  ASSERT_TRUE(!d.should_publish("dev/state/fan_mode", "heat", 1));
}

TEST_CASE(publish_dedup_min_interval_throttles_changing_value) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("dev/state/uptime_s", "10", 10000, 60000));
  ASSERT_TRUE(!d.should_publish("dev/state/uptime_s", "20", 20000, 60000));
  ASSERT_TRUE(!d.should_publish("dev/state/uptime_s", "60", 60000, 60000));
  ASSERT_TRUE(d.should_publish("dev/state/uptime_s", "70", 70000, 60000));   // 60 s after the send
  ASSERT_TRUE(!d.should_publish("dev/state/uptime_s", "80", 80000, 60000));  // throttle restarts
}

TEST_CASE(publish_dedup_throttled_value_is_not_recorded) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("t", "a", 0, 60000));
  ASSERT_TRUE(!d.should_publish("t", "b", 1000, 60000));  // skipped: broker still has "a"
  ASSERT_TRUE(!d.should_publish("t", "a", 2000, 60000));  // back to what the broker holds
  ASSERT_TRUE(d.should_publish("t", "b", 61000, 60000));
}

TEST_CASE(publish_dedup_reset_resends_everything) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("a", "1", 0));
  ASSERT_TRUE(d.should_publish("b", "2", 0));
  ASSERT_EQ(d.size(), static_cast<size_t>(2));
  d.reset();
  ASSERT_EQ(d.size(), static_cast<size_t>(0));
  ASSERT_TRUE(d.should_publish("a", "1", 1));
  ASSERT_TRUE(d.should_publish("b", "2", 1));
}

TEST_CASE(publish_dedup_forget_resends_failed_publish) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("t", "1", 0));
  d.forget("t");  // the publish failed
  ASSERT_TRUE(d.should_publish("t", "1", 1));
  ASSERT_TRUE(!d.should_publish("t", "1", 2));
}

TEST_CASE(publish_dedup_interval_survives_millis_wraparound) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("t", "1", 0xFFFFFF00u, 60000));
  ASSERT_TRUE(!d.should_publish("t", "2", 0x00000100u, 60000));  // 512 ms later, across the wrap
  ASSERT_TRUE(d.should_publish("t", "3", 0x0000EB00u, 60000));   // 60.4 s later
}

TEST_CASE(publish_dedup_empty_payload_is_a_value) {
  MqttPublishDedup d;
  ASSERT_TRUE(d.should_publish("t", "", 0));
  ASSERT_TRUE(!d.should_publish("t", "", 1));
  ASSERT_TRUE(d.should_publish("t", "x", 2));
  ASSERT_TRUE(d.should_publish("t", "", 3));
}

#endif
