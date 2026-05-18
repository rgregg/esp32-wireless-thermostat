#if defined(THERMOSTAT_RUN_TESTS)
#include "thermostat/thermostat_ui_state.h"
#include "test_harness.h"

TEST_CASE(furnace_state_text_windows_open_in_cool_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Windows Open");
}

TEST_CASE(furnace_state_text_windows_open_ignored_in_heat_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::HeatMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Heat);
  ASSERT_STR_EQ(s.c_str(), "Heat mode");
}

TEST_CASE(furnace_state_text_windows_open_ignored_in_off_mode) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::Idle, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Off);
  ASSERT_STR_EQ(s.c_str(), "Idle");
}

TEST_CASE(furnace_state_text_failsafe_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/true, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Failsafe");
}

TEST_CASE(furnace_state_text_lockout_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/true, /*lockout=*/true,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Locked Out");
}

TEST_CASE(furnace_state_text_disconnected_overrides_windows_open) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolMode, /*connected=*/false, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/true, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Not Connected");
}

TEST_CASE(furnace_state_text_no_windows_open_returns_normal) {
  std::string s = thermostat::furnace_state_text(
      FurnaceStateCode::CoolOn, /*connected=*/true, /*lockout=*/false,
      /*failsafe_active=*/false, /*windows_open=*/false, FurnaceMode::Cool);
  ASSERT_STR_EQ(s.c_str(), "Cool on");
}
#endif
