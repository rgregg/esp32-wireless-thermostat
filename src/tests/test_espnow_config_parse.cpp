#if defined(THERMOSTAT_RUN_TESTS)

#include <cstdint>
#include <cstring>

#include "management_paths.h"
#include "test_harness.h"
#include "transport/espnow_config_parse.h"

using namespace thermostat::espnow_config;

namespace {
constexpr uint8_t kCtrl[6] = {0x3C, 0x0F, 0x02, 0xCC, 0xEE, 0x2C};
}  // namespace

TEST_CASE(espnow_cfg_mac_colon_form) {
  uint8_t mac[6] = {};
  ASSERT_TRUE(parse_mac("3C:0F:02:CC:EE:2C", mac));
  ASSERT_TRUE(memcmp(mac, kCtrl, 6) == 0);
  ASSERT_TRUE(parse_mac("3c:0f:02:cc:ee:2c", mac));
  ASSERT_TRUE(memcmp(mac, kCtrl, 6) == 0);
}

// The compact form is what the config pages and /config show for
// paired_controller_mac; storing it back used to silently disable encryption.
TEST_CASE(espnow_cfg_mac_compact_form) {
  uint8_t mac[6] = {};
  ASSERT_TRUE(parse_mac("3C0F02CCEE2C", mac));
  ASSERT_TRUE(memcmp(mac, kCtrl, 6) == 0);
}

TEST_CASE(espnow_cfg_mac_dash_form) {
  uint8_t mac[6] = {};
  ASSERT_TRUE(parse_mac("3C-0F-02-CC-EE-2C", mac));
  ASSERT_TRUE(memcmp(mac, kCtrl, 6) == 0);
}

TEST_CASE(espnow_cfg_mac_rejects_malformed) {
  uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t before[6] = {1, 2, 3, 4, 5, 6};
  ASSERT_TRUE(!parse_mac(nullptr, mac));
  ASSERT_TRUE(!parse_mac("", mac));
  ASSERT_TRUE(!parse_mac("3C0F02CCEE2", mac));          // 11 hex
  ASSERT_TRUE(!parse_mac("3C0F02CCEE2C0", mac));        // 13 hex
  ASSERT_TRUE(!parse_mac("3C:0F:02:CC:EE:2G", mac));    // non-hex
  ASSERT_TRUE(!parse_mac("3C:0F-02:CC:EE:2C", mac));    // mixed separators
  ASSERT_TRUE(!parse_mac("3C.0F.02.CC.EE.2C", mac));    // unknown separator
  ASSERT_TRUE(!parse_mac("3C:0F:02:CC:EE:2C ", mac));   // trailing space
  ASSERT_TRUE(!parse_mac("3C0F02CCEE2Z", mac));
  ASSERT_TRUE(memcmp(mac, before, 6) == 0);
}

TEST_CASE(espnow_cfg_mac_normalize) {
  char out[18];
  ASSERT_TRUE(normalize_mac("3c0f02ccee2c", out));
  ASSERT_STR_EQ(out, "3C:0F:02:CC:EE:2C");
  ASSERT_TRUE(normalize_mac("3c-0f-02-cc-ee-2c", out));
  ASSERT_STR_EQ(out, "3C:0F:02:CC:EE:2C");
  ASSERT_TRUE(!normalize_mac("nope", out));
}

TEST_CASE(espnow_cfg_lmk_parse) {
  uint8_t lmk[16] = {};
  ASSERT_TRUE(parse_lmk("f0e1d2c3b4a596870718293a4b5c6d7e", lmk));
  ASSERT_EQ(lmk[0], 0xF0);
  ASSERT_EQ(lmk[15], 0x7E);
  ASSERT_TRUE(parse_lmk("F0E1D2C3B4A596870718293A4B5C6D7E", lmk));
}

TEST_CASE(espnow_cfg_lmk_rejects_malformed) {
  uint8_t lmk[16] = {};
  ASSERT_TRUE(!parse_lmk(nullptr, lmk));
  ASSERT_TRUE(!parse_lmk("", lmk));
  ASSERT_TRUE(!parse_lmk("f0e1d2c3b4a596870718293a4b5c6d7", lmk));    // 31
  ASSERT_TRUE(!parse_lmk("f0e1d2c3b4a596870718293a4b5c6d7e0", lmk));  // 33
  ASSERT_TRUE(!parse_lmk("g0e1d2c3b4a596870718293a4b5c6d7e", lmk));   // non-hex
  // sscanf("%02x") used to accept a sign or space inside a byte pair.
  ASSERT_TRUE(!parse_lmk("+0e1d2c3b4a596870718293a4b5c6d7e", lmk));
  ASSERT_TRUE(!parse_lmk(" 0e1d2c3b4a596870718293a4b5c6d7e", lmk));
}

TEST_CASE(espnow_cfg_lmk_value_valid) {
  ASSERT_TRUE(lmk_value_valid(""));
  ASSERT_TRUE(lmk_value_valid("f0e1d2c3b4a596870718293a4b5c6d7e"));
  ASSERT_TRUE(!lmk_value_valid("short"));
  ASSERT_TRUE(!lmk_value_valid(nullptr));
}

// The config forms render secrets as blank password inputs ("Leave blank to keep"),
// and FormData posts the blank. A blank secret must be skipped, not stored.
TEST_CASE(espnow_cfg_blank_secret_form_value_is_kept) {
  using thermostat::management_paths::is_blank_secret_form_value;
  ASSERT_TRUE(is_blank_secret_form_value("espnow_lmk", ""));
  ASSERT_TRUE(is_blank_secret_form_value("wifi_password", ""));
  ASSERT_TRUE(is_blank_secret_form_value("mqtt_password", ""));
  ASSERT_TRUE(is_blank_secret_form_value("pirateweather_api_key", ""));
  ASSERT_TRUE(!is_blank_secret_form_value("espnow_lmk", "f0e1d2c3b4a596870718293a4b5c6d7e"));
  ASSERT_TRUE(!is_blank_secret_form_value("mqtt_host", ""));
  ASSERT_TRUE(is_blank_secret_form_value("espnow_lmk", nullptr));
}

#endif  // THERMOSTAT_RUN_TESTS
