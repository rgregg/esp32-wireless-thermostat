#pragma once

#include <stdint.h>

#include "thermostat_types.h"

namespace thermostat {

enum class ThermostatPage : uint8_t {
  Home = 0,
  Fan = 1,
  Mode = 2,
  Settings = 3,
  Screensaver = 4,
};

class ThermostatScreenController {
 public:
  void set_display_timeout_ms(uint32_t timeout_ms) { display_timeout_ms_ = timeout_ms; }

  void on_boot(uint32_t now_ms);
  void on_tab_selected(ThermostatPage page, uint32_t now_ms);
  void on_user_interaction(uint32_t now_ms);
  void on_mode_changed(FurnaceMode mode);

  void tick(uint32_t now_ms);

  ThermostatPage current_page() const { return current_page_; }
  bool screensaver_active() const { return screensaver_active_; }
  bool setpoint_visible() const { return setpoint_visible_; }

 private:
  void evaluate_setpoint_visibility();

  uint32_t display_timeout_ms_ = 300000;
  uint32_t last_interaction_ms_ = 0;

  FurnaceMode current_mode_ = FurnaceMode::Off;
  ThermostatPage current_page_ = ThermostatPage::Home;
  ThermostatPage last_non_screensaver_page_ = ThermostatPage::Home;

  bool screensaver_active_ = false;
  bool setpoint_visible_ = false;
};

}  // namespace thermostat
