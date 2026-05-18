#if defined(THERMOSTAT_RUN_TESTS)
#include <cstdint>
#include "device_registry.h"
#include "discovery_topics.h"
#include "test_harness.h"

// ── DeviceRegistry: upsert with timestamp ──

TEST_CASE(device_registry_upsert_sets_last_seen) {
  DeviceRegistry reg;
  ASSERT_TRUE(reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000));
  ASSERT_EQ(reg.entries[0].last_seen_ms, static_cast<uint32_t>(1000));
}

TEST_CASE(device_registry_upsert_updates_last_seen) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000);
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 5000);
  ASSERT_EQ(reg.entries[0].last_seen_ms, static_cast<uint32_t>(5000));
}

// ── DeviceRegistry: touch ──

TEST_CASE(device_registry_touch_updates_timestamp) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "display", "1.2.3.4", 1000);
  ASSERT_TRUE(reg.touch("AA:BB:CC:DD:EE:FF", 9000));
  ASSERT_EQ(reg.entries[0].last_seen_ms, static_cast<uint32_t>(9000));
}

TEST_CASE(device_registry_touch_returns_false_for_unknown_mac) {
  DeviceRegistry reg;
  ASSERT_TRUE(!reg.touch("11:22:33:44:55:66", 1000));
}

TEST_CASE(device_registry_touch_does_not_change_other_fields) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "my_device", "controller", "10.0.0.1", 100);
  reg.touch("AA:BB:CC:DD:EE:FF", 5000);
  ASSERT_STR_EQ(reg.entries[0].name, "my_device");
  ASSERT_STR_EQ(reg.entries[0].type, "controller");
  ASSERT_STR_EQ(reg.entries[0].ip, "10.0.0.1");
}

// ── DeviceRegistry: remove ──

TEST_CASE(device_registry_remove_clears_entry) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000);
  ASSERT_EQ(reg.count(), static_cast<size_t>(1));
  ASSERT_TRUE(reg.remove("AA:BB:CC:DD:EE:FF"));
  ASSERT_EQ(reg.count(), static_cast<size_t>(0));
  ASSERT_TRUE(!reg.entries[0].occupied);
}

TEST_CASE(device_registry_remove_returns_false_for_unknown) {
  DeviceRegistry reg;
  ASSERT_TRUE(!reg.remove("11:22:33:44:55:66"));
}

TEST_CASE(device_registry_remove_frees_slot_for_reuse) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000);
  reg.remove("AA:BB:CC:DD:EE:FF");
  ASSERT_TRUE(reg.upsert("11:22:33:44:55:66", "dev2", "display", "5.6.7.8", 2000));
  ASSERT_EQ(reg.count(), static_cast<size_t>(1));
  ASSERT_STR_EQ(reg.entries[0].mac, "11:22:33:44:55:66");
}

// ── DeviceRegistry: is_stale ──

TEST_CASE(device_registry_is_stale_true_when_expired) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000);
  // 8 days later
  uint32_t now = 1000 + 8UL * 24 * 60 * 60 * 1000;
  uint32_t max_age = 7UL * 24 * 60 * 60 * 1000;
  ASSERT_TRUE(reg.is_stale(0, now, max_age));
}

TEST_CASE(device_registry_is_stale_false_when_fresh) {
  DeviceRegistry reg;
  reg.upsert("AA:BB:CC:DD:EE:FF", "dev1", "controller", "1.2.3.4", 1000);
  uint32_t now = 1000 + 3600000;  // 1 hour later
  uint32_t max_age = 7UL * 24 * 60 * 60 * 1000;
  ASSERT_TRUE(!reg.is_stale(0, now, max_age));
}

TEST_CASE(device_registry_is_stale_false_for_empty_slot) {
  DeviceRegistry reg;
  uint32_t max_age = 7UL * 24 * 60 * 60 * 1000;
  ASSERT_TRUE(!reg.is_stale(0, 999999999, max_age));
}

TEST_CASE(device_registry_is_stale_false_for_out_of_bounds) {
  DeviceRegistry reg;
  ASSERT_TRUE(!reg.is_stale(kMaxRegistryEntries + 1, 0, 0));
}

// ── mac_strip_colons ──

TEST_CASE(mac_strip_colons_removes_colons) {
  char out[13];
  mac_strip_colons("AA:BB:CC:DD:EE:FF", out, sizeof(out));
  ASSERT_STR_EQ(out, "AABBCCDDEEFF");
}

TEST_CASE(mac_strip_colons_passes_through_compact) {
  char out[13];
  mac_strip_colons("AABBCCDDEEFF", out, sizeof(out));
  ASSERT_STR_EQ(out, "AABBCCDDEEFF");
}

TEST_CASE(mac_strip_colons_handles_null) {
  char out[13] = "dirty";
  mac_strip_colons(nullptr, out, sizeof(out));
  ASSERT_STR_EQ(out, "");
}

// ── discovery_topics.h: entity counts ──

TEST_CASE(discovery_controller_entity_count) {
  ASSERT_EQ(kControllerDiscoveryCount, static_cast<size_t>(23));
}

TEST_CASE(discovery_display_entity_count) {
  ASSERT_EQ(kDisplayDiscoveryCount, static_cast<size_t>(19));
}

TEST_CASE(discovery_entities_for_role_controller) {
  size_t count = 0;
  const auto *ents = discovery_entities_for_role("controller", &count);
  ASSERT_TRUE(ents != nullptr);
  ASSERT_EQ(count, kControllerDiscoveryCount);
}

TEST_CASE(discovery_entities_for_role_display) {
  size_t count = 0;
  const auto *ents = discovery_entities_for_role("display", &count);
  ASSERT_TRUE(ents != nullptr);
  ASSERT_EQ(count, kDisplayDiscoveryCount);
}

TEST_CASE(discovery_entities_for_role_unknown_returns_null) {
  size_t count = 99;
  const auto *ents = discovery_entities_for_role("unknown", &count);
  ASSERT_TRUE(ents == nullptr);
  ASSERT_EQ(count, static_cast<size_t>(0));
}

// ── format_discovery_topic ──

TEST_CASE(format_discovery_topic_climate) {
  char buf[128];
  format_discovery_topic(buf, sizeof(buf),
                         "homeassistant", "climate",
                         "esp32_AABB_controller", "");
  ASSERT_STR_EQ(buf, "homeassistant/climate/esp32_AABB_controller/config");
}

TEST_CASE(format_discovery_topic_sensor_with_suffix) {
  char buf[128];
  format_discovery_topic(buf, sizeof(buf),
                         "homeassistant", "sensor",
                         "esp32_AABB_controller", "_filter_runtime");
  ASSERT_STR_EQ(buf, "homeassistant/sensor/esp32_AABB_controller_filter_runtime/config");
}

#endif
