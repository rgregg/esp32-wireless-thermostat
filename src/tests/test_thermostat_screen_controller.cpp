#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "thermostat/thermostat_screen_controller.h"

TEST_CASE(screen_controller_idle_screensaver_and_resume) {
  thermostat::ThermostatScreenController sc;
  sc.set_display_timeout_ms(5000);
  sc.on_boot(1000);

  sc.on_tab_selected(thermostat::ThermostatPage::Fan, 2000);
  ASSERT_TRUE(sc.current_page() == thermostat::ThermostatPage::Fan);

  sc.tick(8000);
  ASSERT_TRUE(sc.screensaver_active());
  ASSERT_TRUE(sc.current_page() == thermostat::ThermostatPage::Screensaver);

  sc.on_user_interaction(8100);
  ASSERT_TRUE(!sc.screensaver_active());
  ASSERT_TRUE(sc.current_page() == thermostat::ThermostatPage::Fan);
}

TEST_CASE(screen_controller_setpoint_visibility_tracks_mode) {
  thermostat::ThermostatScreenController sc;
  sc.on_boot(0);
  sc.on_mode_changed(FurnaceMode::Off);
  ASSERT_TRUE(!sc.setpoint_visible());

  sc.on_mode_changed(FurnaceMode::Heat);
  ASSERT_TRUE(sc.setpoint_visible());

  sc.on_mode_changed(FurnaceMode::Cool);
  ASSERT_TRUE(sc.setpoint_visible());
}
#endif
