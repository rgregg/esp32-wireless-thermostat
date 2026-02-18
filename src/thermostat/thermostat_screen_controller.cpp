#include "thermostat/thermostat_screen_controller.h"

namespace thermostat {

void ThermostatScreenController::on_boot(uint32_t now_ms) {
  current_page_ = ThermostatPage::Home;
  last_non_screensaver_page_ = ThermostatPage::Home;
  screensaver_active_ = false;
  last_interaction_ms_ = now_ms;
  evaluate_setpoint_visibility();
}

void ThermostatScreenController::on_tab_selected(ThermostatPage page, uint32_t now_ms) {
  if (page == ThermostatPage::Screensaver) {
    return;
  }
  current_page_ = page;
  last_non_screensaver_page_ = page;
  screensaver_active_ = false;
  last_interaction_ms_ = now_ms;
}

void ThermostatScreenController::on_user_interaction(uint32_t now_ms) {
  last_interaction_ms_ = now_ms;
  if (screensaver_active_) {
    screensaver_active_ = false;
    current_page_ = last_non_screensaver_page_;
  }
}

void ThermostatScreenController::on_mode_changed(FurnaceMode mode) {
  current_mode_ = mode;
  evaluate_setpoint_visibility();
}

void ThermostatScreenController::tick(uint32_t now_ms) {
  if (!screensaver_active_ && display_timeout_ms_ > 0 &&
      (now_ms - last_interaction_ms_) >= display_timeout_ms_) {
    screensaver_active_ = true;
    current_page_ = ThermostatPage::Screensaver;
  }
}

void ThermostatScreenController::evaluate_setpoint_visibility() {
  setpoint_visible_ = current_mode_ != FurnaceMode::Off;
}

}  // namespace thermostat
