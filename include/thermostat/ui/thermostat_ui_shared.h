#pragma once

#include <stdint.h>

#if defined(THERMOSTAT_ROLE_THERMOSTAT) || defined(THERMOSTAT_UI_PREVIEW)
#include <lvgl.h>
#include "thermostat_types.h"
#include "thermostat/display_model.h"

namespace thermostat {
namespace ui {

struct UiCallbacks {
  lv_event_cb_t on_tab_changed = nullptr;
  lv_event_cb_t on_setpoint_up = nullptr;
  lv_event_cb_t on_setpoint_down = nullptr;
  lv_event_cb_t on_mode = nullptr;
  lv_event_cb_t on_fan = nullptr;
  lv_event_cb_t on_unit = nullptr;
  lv_event_cb_t on_sync = nullptr;
  lv_event_cb_t on_filter_reset = nullptr;
  lv_event_cb_t on_timeout_slider = nullptr;
  lv_event_cb_t on_brightness_slider = nullptr;
  lv_event_cb_t on_dim_slider = nullptr;
  lv_event_cb_t on_timeout_down = nullptr;
  lv_event_cb_t on_timeout_up = nullptr;
  lv_event_cb_t on_brightness_down = nullptr;
  lv_event_cb_t on_brightness_up = nullptr;
  lv_event_cb_t on_dim_down = nullptr;
  lv_event_cb_t on_dim_up = nullptr;
};

struct UiHandles {
  lv_obj_t *tabs = nullptr;
  lv_obj_t *home_page = nullptr;
  lv_obj_t *fan_page = nullptr;
  lv_obj_t *mode_page = nullptr;
  lv_obj_t *settings_page = nullptr;
  lv_obj_t *screensaver_page = nullptr;
  lv_obj_t *home_date_label = nullptr;
  lv_obj_t *home_time_label = nullptr;
  lv_obj_t *status_label = nullptr;
  lv_obj_t *indoor_label = nullptr;
  lv_obj_t *humidity_label = nullptr;
  lv_obj_t *setpoint_label = nullptr;
  lv_obj_t *weather_label = nullptr;
  lv_obj_t *weather_icon_label = nullptr;
  lv_obj_t *screen_time_label = nullptr;
  lv_obj_t *screen_weather_label = nullptr;
  lv_obj_t *screen_indoor_label = nullptr;
  lv_obj_t *settings_diag_label = nullptr;
  lv_obj_t *settings_display_label = nullptr;
  lv_obj_t *settings_system_label = nullptr;
  lv_obj_t *settings_wifi_label = nullptr;
  lv_obj_t *settings_mqtt_label = nullptr;
  lv_obj_t *settings_controller_label = nullptr;
  lv_obj_t *settings_espnow_label = nullptr;
  lv_obj_t *settings_config_label = nullptr;
  lv_obj_t *settings_errors_label = nullptr;
  lv_obj_t *timeout_slider = nullptr;
  lv_obj_t *brightness_slider = nullptr;
  lv_obj_t *dim_slider = nullptr;
  lv_obj_t *setpoint_column = nullptr;
  lv_obj_t *filter_label = nullptr;
};

void build_thermostat_ui(const UiCallbacks &callbacks, UiHandles *out_handles);
void set_mode_button_state(FurnaceMode mode);
void set_fan_button_state(FanMode mode);
void set_temperature_unit_button_state(TemperatureUnit unit);
void update_screensaver_layout(lv_obj_t *time_label, lv_obj_t *weather_label,
                               lv_obj_t *indoor_label, uint32_t minute_index);

}  // namespace ui
}  // namespace thermostat
#endif
