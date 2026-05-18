#if defined(THERMOSTAT_RUN_TESTS)
#include "mqtt_payload.h"
#include "test_harness.h"

// --- mode_to_str / str_to_mode ---

TEST_CASE(mqtt_payload_mode_to_str_all_modes) {
  ASSERT_STR_EQ(mqtt_payload::mode_to_str(FurnaceMode::Off), "off");
  ASSERT_STR_EQ(mqtt_payload::mode_to_str(FurnaceMode::Heat), "heat");
  ASSERT_STR_EQ(mqtt_payload::mode_to_str(FurnaceMode::Cool), "cool");
}

TEST_CASE(mqtt_payload_mode_to_str_unknown_falls_back_to_off) {
  // Any unmapped enum value must degrade gracefully — controller must never
  // surface an unknown mode string on MQTT.
  ASSERT_STR_EQ(mqtt_payload::mode_to_str(static_cast<FurnaceMode>(99)), "off");
}

TEST_CASE(mqtt_payload_str_to_mode_known_values) {
  ASSERT_TRUE(mqtt_payload::str_to_mode("heat") == FurnaceMode::Heat);
  ASSERT_TRUE(mqtt_payload::str_to_mode("cool") == FurnaceMode::Cool);
  ASSERT_TRUE(mqtt_payload::str_to_mode("off") == FurnaceMode::Off);
}

TEST_CASE(mqtt_payload_str_to_mode_is_case_sensitive) {
  // HA sends lowercase. If this ever flips to case-insensitive we want to
  // notice (downstream code assumes the canonical form).
  ASSERT_TRUE(mqtt_payload::str_to_mode("Heat") == FurnaceMode::Off);
  ASSERT_TRUE(mqtt_payload::str_to_mode("HEAT") == FurnaceMode::Off);
}

TEST_CASE(mqtt_payload_str_to_mode_unknown_falls_back_to_off) {
  ASSERT_TRUE(mqtt_payload::str_to_mode("") == FurnaceMode::Off);
  ASSERT_TRUE(mqtt_payload::str_to_mode("auto") == FurnaceMode::Off);
  ASSERT_TRUE(mqtt_payload::str_to_mode("heat_cool") == FurnaceMode::Off);
}

// --- fan_to_str / str_to_fan ---

TEST_CASE(mqtt_payload_fan_to_str_all_modes) {
  ASSERT_STR_EQ(mqtt_payload::fan_to_str(FanMode::Automatic), "auto");
  ASSERT_STR_EQ(mqtt_payload::fan_to_str(FanMode::AlwaysOn), "on");
  ASSERT_STR_EQ(mqtt_payload::fan_to_str(FanMode::Circulate), "circulate");
}

TEST_CASE(mqtt_payload_fan_to_str_unknown_falls_back_to_auto) {
  ASSERT_STR_EQ(mqtt_payload::fan_to_str(static_cast<FanMode>(99)), "auto");
}

TEST_CASE(mqtt_payload_str_to_fan_known_values) {
  ASSERT_TRUE(mqtt_payload::str_to_fan("on") == FanMode::AlwaysOn);
  ASSERT_TRUE(mqtt_payload::str_to_fan("circulate") == FanMode::Circulate);
  ASSERT_TRUE(mqtt_payload::str_to_fan("auto") == FanMode::Automatic);
}

TEST_CASE(mqtt_payload_str_to_fan_always_on_alias) {
  // "always on" is accepted as a synonym for "on". Pin this so we don't
  // accidentally drop the alias and break older HA configs.
  ASSERT_TRUE(mqtt_payload::str_to_fan("always on") == FanMode::AlwaysOn);
}

TEST_CASE(mqtt_payload_str_to_fan_unknown_falls_back_to_auto) {
  ASSERT_TRUE(mqtt_payload::str_to_fan("") == FanMode::Automatic);
  ASSERT_TRUE(mqtt_payload::str_to_fan("off") == FanMode::Automatic);
  ASSERT_TRUE(mqtt_payload::str_to_fan("ON") == FanMode::Automatic);
}

// --- furnace_state_to_str / str_to_furnace_state ---

TEST_CASE(mqtt_payload_furnace_state_to_str_all_states) {
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::Idle), "Idle");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::HeatMode), "Heat Standby");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::HeatOn), "Heating");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::CoolMode), "Cool Standby");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::CoolOn), "Cooling");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::FanOn), "Fan Running");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::Error), "Error");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::HeatWait), "Heat Wait");
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(FurnaceStateCode::CoolWait), "Cool Wait");
}

TEST_CASE(mqtt_payload_furnace_state_to_str_unknown) {
  ASSERT_STR_EQ(mqtt_payload::furnace_state_to_str(static_cast<FurnaceStateCode>(99)),
                "Unknown");
}

TEST_CASE(mqtt_payload_str_to_furnace_state_roundtrip) {
  const FurnaceStateCode kStates[] = {
      FurnaceStateCode::Idle,      FurnaceStateCode::HeatMode,
      FurnaceStateCode::HeatOn,    FurnaceStateCode::CoolMode,
      FurnaceStateCode::CoolOn,    FurnaceStateCode::FanOn,
      FurnaceStateCode::HeatWait,  FurnaceStateCode::CoolWait,
  };
  for (FurnaceStateCode s : kStates) {
    const char *text = mqtt_payload::furnace_state_to_str(s);
    ASSERT_TRUE(mqtt_payload::str_to_furnace_state(text) == s);
  }
}

TEST_CASE(mqtt_payload_str_to_furnace_state_unknown_returns_error) {
  // Any string we don't recognize must map to Error so a corrupted retained
  // message can't silently look like Idle.
  ASSERT_TRUE(mqtt_payload::str_to_furnace_state("") == FurnaceStateCode::Error);
  ASSERT_TRUE(mqtt_payload::str_to_furnace_state("idle") == FurnaceStateCode::Error);
  ASSERT_TRUE(mqtt_payload::str_to_furnace_state("Standby") == FurnaceStateCode::Error);
}

// --- parse_bool ---

TEST_CASE(mqtt_payload_parse_bool_truthy_values) {
  ASSERT_TRUE(mqtt_payload::parse_bool("1"));
  ASSERT_TRUE(mqtt_payload::parse_bool("true"));
  ASSERT_TRUE(mqtt_payload::parse_bool("on"));
}

TEST_CASE(mqtt_payload_parse_bool_falsy_values) {
  ASSERT_TRUE(!mqtt_payload::parse_bool("0"));
  ASSERT_TRUE(!mqtt_payload::parse_bool("false"));
  ASSERT_TRUE(!mqtt_payload::parse_bool("off"));
  ASSERT_TRUE(!mqtt_payload::parse_bool(""));
}

TEST_CASE(mqtt_payload_parse_bool_handles_nullptr) {
  // Some callers (e.g. retained-message clears) pass nullptr; must not crash.
  ASSERT_TRUE(!mqtt_payload::parse_bool(nullptr));
}

TEST_CASE(mqtt_payload_parse_bool_is_case_sensitive) {
  // Pin existing behavior: only lowercase canonical forms are truthy.
  ASSERT_TRUE(!mqtt_payload::parse_bool("True"));
  ASSERT_TRUE(!mqtt_payload::parse_bool("ON"));
  ASSERT_TRUE(!mqtt_payload::parse_bool("YES"));
}

TEST_CASE(mqtt_payload_parse_bool_rejects_unknown) {
  ASSERT_TRUE(!mqtt_payload::parse_bool("2"));
  ASSERT_TRUE(!mqtt_payload::parse_bool("enabled"));
  ASSERT_TRUE(!mqtt_payload::parse_bool(" on"));
}

#endif
