#if defined(THERMOSTAT_RUN_TESTS)
#include "mqtt_topics.h"
#include "test_harness.h"

TEST_CASE(mqtt_topics_device_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
                            "home/thermostat", "AABBCCDDEEFF", "status");
  ASSERT_STR_EQ(buf, "home/thermostat/devices/AABBCCDDEEFF/status");
}

TEST_CASE(mqtt_topics_device_topic_truncates_safely) {
  char buf[16];
  int rc = mqtt_topics::device_topic(buf, sizeof(buf),
                                     "home/thermostat", "AABBCCDDEEFF", "status");
  // snprintf returns what would have been written (without truncation)
  ASSERT_TRUE(rc >= 16);
  // buf must be null-terminated and not overflow
  ASSERT_EQ(buf[15], '\0');
  ASSERT_EQ(strlen(buf), static_cast<size_t>(15));
}

TEST_CASE(mqtt_topics_announce_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
                            "home/thermostat", "112233445566", "announce");
  ASSERT_STR_EQ(buf, "home/thermostat/devices/112233445566/announce");
}

TEST_CASE(mqtt_topics_peer_topic) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
                            "home/thermostat", "DEADBEEF0001", "temperature");
  ASSERT_STR_EQ(buf, "home/thermostat/devices/DEADBEEF0001/temperature");
}

TEST_CASE(mqtt_topics_wildcard_subscribe) {
  char buf[128];
  mqtt_topics::device_topic(buf, sizeof(buf),
                            "home/thermostat", "+", "command");
  ASSERT_STR_EQ(buf, "home/thermostat/devices/+/command");
}

TEST_CASE(mqtt_topics_client_id) {
  char buf[64];
  mqtt_topics::client_id(buf, sizeof(buf), "thermostat", "AABBCCDDEEFF");
  ASSERT_STR_EQ(buf, "thermostat-AABBCCDDEEFF");
}

#endif
