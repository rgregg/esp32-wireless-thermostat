#if defined(THERMOSTAT_RUN_TESTS)
#include "management_paths.h"
#include "test_harness.h"

TEST_CASE(management_paths_parse_cfg_topics) {
  std::string key;
  ASSERT_TRUE(thermostat::management_paths::parse_cfg_set_topic(
      "thermostat/furnace-controller",
      "thermostat/furnace-controller/cfg/display_mqtt_base_topic/set", &key));
  ASSERT_EQ(key, std::string("display_mqtt_base_topic"));

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
  ASSERT_TRUE(!thermostat::management_paths::parse_cfg_set_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/../mqtt_password/set", &key));
  ASSERT_TRUE(!thermostat::management_paths::parse_cfg_set_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/mqtt password/set", &key));
  ASSERT_TRUE(!thermostat::management_paths::parse_cfg_state_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/ota$password/state", &key));

  ASSERT_TRUE(thermostat::management_paths::parse_cfg_state_topic(
      "thermostat/furnace-display",
      "thermostat/furnace-display/cfg/pirateweather_api_key/state", &key));
  ASSERT_EQ(key, std::string("pirateweather_api_key"));
}

TEST_CASE(management_paths_parse_form_keys) {
  std::string key;
  ASSERT_TRUE(thermostat::management_paths::parse_prefixed_form_key(
      "disp_display_timeout_s", "disp_", &key));
  ASSERT_EQ(key, std::string("display_timeout_s"));
  ASSERT_TRUE(thermostat::management_paths::parse_prefixed_form_key(
      "disp_espnow-peer-mac", "disp_", &key));
  ASSERT_EQ(key, std::string("espnow-peer-mac"));
  ASSERT_TRUE(!thermostat::management_paths::parse_prefixed_form_key(
      "display_timeout_s", "disp_", &key));
  ASSERT_TRUE(!thermostat::management_paths::parse_prefixed_form_key(
      "disp_../wifi_password", "disp_", &key));
  ASSERT_TRUE(!thermostat::management_paths::parse_prefixed_form_key(
      "disp_mqtt host", "disp_", &key));
}

TEST_CASE(management_paths_secret_key_classification) {
  ASSERT_TRUE(thermostat::management_paths::is_secret_cfg_key("wifi_password"));
  ASSERT_TRUE(thermostat::management_paths::is_secret_cfg_key("mqtt_password"));
  ASSERT_TRUE(thermostat::management_paths::is_secret_cfg_key("espnow_lmk"));
  ASSERT_TRUE(thermostat::management_paths::is_secret_cfg_key("pirateweather_api_key"));
  ASSERT_TRUE(thermostat::management_paths::is_secret_cfg_key("web_password"));

  ASSERT_TRUE(!thermostat::management_paths::is_secret_cfg_key("wifi_ssid"));
  ASSERT_TRUE(!thermostat::management_paths::is_secret_cfg_key("espnow_peer_mac"));
  ASSERT_TRUE(!thermostat::management_paths::is_secret_cfg_key("display_timeout_s"));
}

#endif
