#if defined(THERMOSTAT_RUN_TESTS)
#include "provisioning_gate.h"
#include "test_harness.h"

// Fresh device: WiFi enabled, no stored SSID, no baked default -> provisioning needed.
TEST_CASE(provisioning_gate_fresh_device) {
  ASSERT_TRUE(provisioning_gate::needed(false, "", ""));
}

// Stored SSID present -> not needed (NVS creds win).
TEST_CASE(provisioning_gate_stored_ssid) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "MyNet", ""));
}

// Baked default SSID suppresses provisioning even with empty NVS.
TEST_CASE(provisioning_gate_baked_default) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "", "BakedNet"));
}

// NVS SSID wins over baked default; still not needed.
TEST_CASE(provisioning_gate_nvs_over_baked) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "MyNet", "BakedNet"));
}

// ESP-NOW-only mode (WiFi disabled): never provision, regardless of SSID state.
TEST_CASE(provisioning_gate_wifi_disabled) {
  ASSERT_TRUE(!provisioning_gate::needed(true, "", ""));
}

// Null NVS pointer (key absent) behaves like empty string.
TEST_CASE(provisioning_gate_null_nvs_ssid) {
  ASSERT_TRUE(provisioning_gate::needed(false, nullptr, ""));
  ASSERT_TRUE(!provisioning_gate::needed(false, nullptr, "BakedNet"));
}

// wifi_disabled short-circuits even when SSIDs are present.
TEST_CASE(provisioning_gate_wifi_disabled_ignores_ssid) {
  ASSERT_TRUE(!provisioning_gate::needed(true, "SomeNet", "BakedNet"));
}

// Null baked default behaves like empty: no effective SSID -> needed.
TEST_CASE(provisioning_gate_null_baked_ssid) {
  ASSERT_TRUE(provisioning_gate::needed(false, "", nullptr));
  ASSERT_TRUE(provisioning_gate::needed(false, nullptr, nullptr));
}

#endif
