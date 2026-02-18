#if defined(THERMOSTAT_RUN_TESTS)
#include "management_paths.h"
#include "test_harness.h"

TEST_CASE(management_paths_parse_cfg_topics) {
  std::string key;
  ASSERT_TRUE(thermostat::management_paths::parse_cfg_set_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/mqtt_host/set", &key));
  ASSERT_EQ(key, std::string("mqtt_host"));

  ASSERT_TRUE(thermostat::management_paths::parse_cfg_state_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/reboot_required/state", &key));
  ASSERT_EQ(key, std::string("reboot_required"));

  ASSERT_TRUE(!thermostat::management_paths::parse_cfg_set_topic(
      "thermostat/furnace-display", "thermostat/furnace-display/cfg/set", &key));
  ASSERT_TRUE(!thermostat::management_paths::parse_cfg_state_topic(
      "thermostat/furnace-display", "thermostat/furnace-controller/cfg/mqtt_host/state", &key));
}

TEST_CASE(management_paths_parse_form_keys) {
  std::string key;
  ASSERT_TRUE(thermostat::management_paths::parse_prefixed_form_key(
      "disp_display_timeout_s", "disp_", &key));
  ASSERT_EQ(key, std::string("display_timeout_s"));
  ASSERT_TRUE(!thermostat::management_paths::parse_prefixed_form_key(
      "display_timeout_s", "disp_", &key));
}

#endif

