#include "thermostat/ui/thermostat_ui_shared.h"
#include "thermostat_types.h"
#include "thermostat/display_model.h"

#if defined(THERMOSTAT_ROLE_THERMOSTAT) || defined(THERMOSTAT_UI_PREVIEW)

namespace thermostat {
namespace ui {

extern "C" const lv_font_t thermostat_font_montserrat_20;
extern "C" const lv_font_t thermostat_font_montserrat_26;
extern "C" const lv_font_t thermostat_font_montserrat_28;
extern "C" const lv_font_t thermostat_font_montserrat_30;
extern "C" const lv_font_t thermostat_font_montserrat_40;
extern "C" const lv_font_t thermostat_font_montserrat_48;
extern "C" const lv_font_t thermostat_font_montserrat_80;
extern "C" const lv_font_t thermostat_font_montserrat_120;

namespace {

constexpr int kDisplayWidth = 800;
constexpr int kDisplayHeight = 480;
constexpr uint32_t kColorPageBg = 0x111827;
constexpr uint32_t kColorHeaderGrad = 0x005782;
constexpr uint32_t kColorHeaderBorder = 0x0077B3;
constexpr uint32_t kColorActionBtn = 0x4F46E5;
constexpr uint32_t kColorWhite = 0xFFFFFF;
constexpr uint32_t kColorBlack = 0x000000;

lv_style_t g_style_page;
lv_style_t g_style_header_item;
lv_style_t g_style_header_item_checked;
lv_style_t g_style_tab_item;
lv_style_t g_style_tab_item_checked;
lv_style_t g_style_primary_button;
lv_style_t g_style_text_white;
bool g_styles_ready = false;
lv_obj_t *g_fan_auto_btn = nullptr;
lv_obj_t *g_fan_on_btn = nullptr;
lv_obj_t *g_fan_circ_btn = nullptr;
lv_obj_t *g_mode_heat_btn = nullptr;
lv_obj_t *g_mode_cool_btn = nullptr;
lv_obj_t *g_mode_off_btn = nullptr;
lv_obj_t *g_unit_f_btn = nullptr;
lv_obj_t *g_unit_c_btn = nullptr;
lv_event_cb_t g_fan_external_cb = nullptr;
lv_event_cb_t g_mode_external_cb = nullptr;
lv_event_cb_t g_unit_external_cb = nullptr;

const lv_font_t *font20() { return &thermostat_font_montserrat_20; }

const lv_font_t *font26() { return &thermostat_font_montserrat_26; }

const lv_font_t *font28() { return &thermostat_font_montserrat_28; }

const lv_font_t *font30() { return &thermostat_font_montserrat_30; }

const lv_font_t *font40() { return &thermostat_font_montserrat_40; }

const lv_font_t *font48() { return &thermostat_font_montserrat_48; }

const lv_font_t *font120() { return &thermostat_font_montserrat_120; }

const lv_font_t *font80() { return &thermostat_font_montserrat_80; }

void init_styles() {
  if (g_styles_ready) {
    return;
  }
  g_styles_ready = true;

  lv_style_init(&g_style_page);
  lv_style_set_bg_color(&g_style_page, lv_color_hex(kColorPageBg));
  lv_style_set_bg_opa(&g_style_page, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_page, 0);
  lv_style_set_pad_all(&g_style_page, 0);

  lv_style_init(&g_style_header_item);
  lv_style_set_bg_color(&g_style_header_item, lv_color_hex(kColorPageBg));
  lv_style_set_bg_grad_color(&g_style_header_item, lv_color_hex(kColorHeaderGrad));
  lv_style_set_bg_grad_dir(&g_style_header_item, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&g_style_header_item, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_header_item, 1);
  lv_style_set_border_color(&g_style_header_item, lv_color_hex(kColorHeaderBorder));
  lv_style_set_radius(&g_style_header_item, 0);
  lv_style_set_text_color(&g_style_header_item, lv_color_hex(kColorWhite));
  lv_style_set_text_font(&g_style_header_item, font20());

  lv_style_init(&g_style_header_item_checked);
  lv_style_set_bg_color(&g_style_header_item_checked, lv_color_hex(kColorActionBtn));
  lv_style_set_bg_opa(&g_style_header_item_checked, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_header_item_checked, 2);
  lv_style_set_border_color(&g_style_header_item_checked, lv_color_hex(kColorWhite));
  lv_style_set_radius(&g_style_header_item_checked, 0);
  lv_style_set_text_color(&g_style_header_item_checked, lv_color_hex(kColorWhite));
  lv_style_set_text_font(&g_style_header_item_checked, font20());

  lv_style_init(&g_style_tab_item);
  lv_style_set_bg_color(&g_style_tab_item, lv_color_hex(kColorPageBg));
  lv_style_set_bg_grad_color(&g_style_tab_item, lv_color_hex(kColorHeaderGrad));
  lv_style_set_bg_grad_dir(&g_style_tab_item, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&g_style_tab_item, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_tab_item, 1);
  lv_style_set_border_color(&g_style_tab_item, lv_color_hex(kColorHeaderBorder));
  lv_style_set_radius(&g_style_tab_item, 0);
  lv_style_set_text_color(&g_style_tab_item, lv_color_hex(kColorWhite));
  lv_style_set_text_font(&g_style_tab_item, font20());

  lv_style_init(&g_style_tab_item_checked);
  lv_style_set_bg_color(&g_style_tab_item_checked, lv_color_hex(kColorActionBtn));
  lv_style_set_bg_opa(&g_style_tab_item_checked, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_tab_item_checked, 2);
  lv_style_set_border_color(&g_style_tab_item_checked, lv_color_hex(kColorWhite));
  lv_style_set_radius(&g_style_tab_item_checked, 0);
  lv_style_set_text_color(&g_style_tab_item_checked, lv_color_hex(kColorWhite));
  lv_style_set_text_font(&g_style_tab_item_checked, font20());

  lv_style_init(&g_style_primary_button);
  lv_style_set_bg_color(&g_style_primary_button, lv_color_hex(kColorActionBtn));
  lv_style_set_bg_opa(&g_style_primary_button, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_primary_button, 0);
  lv_style_set_radius(&g_style_primary_button, 0);
  lv_style_set_text_color(&g_style_primary_button, lv_color_hex(kColorWhite));

  lv_style_init(&g_style_text_white);
  lv_style_set_text_color(&g_style_text_white, lv_color_hex(kColorWhite));
}

void style_label(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_add_style(label, &g_style_text_white, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
}

void style_indoor_temp_label(lv_obj_t *label) {
  style_label(label, font120());
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

void style_primary_button(lv_obj_t *button) {
  lv_obj_add_style(button, &g_style_primary_button, LV_PART_MAIN);
}

void style_settings_button(lv_obj_t *button) {
  lv_obj_add_style(button, &g_style_header_item, LV_PART_MAIN);
}

void style_settings_slider(lv_obj_t *slider) {
  lv_obj_set_height(slider, 20);
  lv_obj_set_style_bg_color(slider, lv_color_hex(kColorBlack), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(slider, lv_color_hex(kColorHeaderBorder), LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);

  lv_obj_set_style_bg_color(slider, lv_color_hex(kColorHeaderGrad), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);

  lv_obj_set_style_bg_color(slider, lv_color_hex(kColorWhite), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
}

lv_obj_t *make_transparent(lv_obj_t *parent, lv_coord_t width, lv_coord_t height) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_set_size(obj, width, height);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
  return obj;
}

lv_obj_t *make_page() {
  lv_obj_t *p = lv_obj_create(lv_scr_act());
  lv_obj_set_size(p, kDisplayWidth, kDisplayHeight - 50);
  lv_obj_set_pos(p, 0, 50);
  lv_obj_add_style(p, &g_style_page, LV_PART_MAIN);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(p, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
  return p;
}

void apply_mode_state(FurnaceMode mode) {
  if (g_mode_heat_btn == nullptr || g_mode_cool_btn == nullptr || g_mode_off_btn == nullptr) {
    return;
  }
  lv_obj_clear_state(g_mode_heat_btn, LV_STATE_CHECKED);
  lv_obj_clear_state(g_mode_cool_btn, LV_STATE_CHECKED);
  lv_obj_clear_state(g_mode_off_btn, LV_STATE_CHECKED);

  if (mode == FurnaceMode::Heat) {
    lv_obj_add_state(g_mode_heat_btn, LV_STATE_CHECKED);
  } else if (mode == FurnaceMode::Cool) {
    lv_obj_add_state(g_mode_cool_btn, LV_STATE_CHECKED);
  } else {
    lv_obj_add_state(g_mode_off_btn, LV_STATE_CHECKED);
  }
}

void apply_fan_state(FanMode mode) {
  if (g_fan_auto_btn == nullptr || g_fan_on_btn == nullptr || g_fan_circ_btn == nullptr) {
    return;
  }
  lv_obj_clear_state(g_fan_auto_btn, LV_STATE_CHECKED);
  lv_obj_clear_state(g_fan_on_btn, LV_STATE_CHECKED);
  lv_obj_clear_state(g_fan_circ_btn, LV_STATE_CHECKED);

  if (mode == FanMode::AlwaysOn) {
    lv_obj_add_state(g_fan_on_btn, LV_STATE_CHECKED);
  } else if (mode == FanMode::Circulate) {
    lv_obj_add_state(g_fan_circ_btn, LV_STATE_CHECKED);
  } else {
    lv_obj_add_state(g_fan_auto_btn, LV_STATE_CHECKED);
  }
}

void fan_button_cb(lv_event_t *e) {
  auto mode = static_cast<FanMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  apply_fan_state(mode);
  if (g_fan_external_cb != nullptr) {
    g_fan_external_cb(e);
  }
}

void apply_temperature_unit_state(TemperatureUnit unit) {
  if (g_unit_f_btn == nullptr || g_unit_c_btn == nullptr) {
    return;
  }
  lv_obj_clear_state(g_unit_f_btn, LV_STATE_CHECKED);
  lv_obj_clear_state(g_unit_c_btn, LV_STATE_CHECKED);

  if (unit == TemperatureUnit::Fahrenheit) {
    lv_obj_add_state(g_unit_f_btn, LV_STATE_CHECKED);
  } else {
    lv_obj_add_state(g_unit_c_btn, LV_STATE_CHECKED);
  }
}

void unit_button_cb(lv_event_t *e) {
  auto unit = static_cast<TemperatureUnit>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  apply_temperature_unit_state(unit);
  if (g_unit_external_cb != nullptr) {
    g_unit_external_cb(e);
  }
}

void mode_button_cb(lv_event_t *e) {
  auto mode = static_cast<FurnaceMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  apply_mode_state(mode);
  if (g_mode_external_cb != nullptr) {
    g_mode_external_cb(e);
  }
}

}  // namespace

void build_thermostat_ui(const UiCallbacks &callbacks, UiHandles *out_handles) {
  if (out_handles == nullptr) {
    return;
  }
  init_styles();
  g_fan_external_cb = callbacks.on_fan;
  g_mode_external_cb = callbacks.on_mode;
  g_unit_external_cb = callbacks.on_unit;
  g_fan_auto_btn = nullptr;
  g_fan_on_btn = nullptr;
  g_fan_circ_btn = nullptr;
  g_mode_heat_btn = nullptr;
  g_mode_cool_btn = nullptr;
  g_mode_off_btn = nullptr;
  g_unit_f_btn = nullptr;
  g_unit_c_btn = nullptr;
  lv_obj_add_style(lv_scr_act(), &g_style_page, LV_PART_MAIN);

  out_handles->tabs = lv_btnmatrix_create(lv_scr_act());
  static const char *tab_map[] = {"HOME", "FAN", "MODE", "SETTINGS", ""};
  lv_btnmatrix_set_map(out_handles->tabs, tab_map);
  lv_btnmatrix_set_btn_ctrl_all(out_handles->tabs, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(out_handles->tabs, true);
  lv_btnmatrix_set_btn_ctrl(out_handles->tabs, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(out_handles->tabs, kDisplayWidth, 50);
  lv_obj_set_style_bg_color(out_handles->tabs, lv_color_hex(kColorBlack), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(out_handles->tabs, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(out_handles->tabs, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(out_handles->tabs, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(out_handles->tabs, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_top(out_handles->tabs, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(out_handles->tabs, 2, LV_PART_MAIN);
  lv_obj_add_style(out_handles->tabs, &g_style_tab_item, LV_PART_ITEMS);
  lv_obj_add_style(out_handles->tabs, &g_style_tab_item_checked, LV_PART_ITEMS | LV_STATE_CHECKED);
  if (callbacks.on_tab_changed != nullptr) {
    lv_obj_add_event_cb(out_handles->tabs, callbacks.on_tab_changed, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  out_handles->home_page = make_page();
  out_handles->fan_page = make_page();
  out_handles->mode_page = make_page();
  out_handles->settings_page = make_page();
  out_handles->screensaver_page = make_page();

  lv_obj_t *home_root = make_transparent(out_handles->home_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(home_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(home_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(home_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_left(home_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(home_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_top(home_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(home_root, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_column(home_root, 0, LV_PART_MAIN);

  lv_obj_t *left_col = make_transparent(home_root, 360, LV_PCT(100));
  lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  out_handles->home_date_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->home_date_label, "--- date ---");
  style_label(out_handles->home_date_label, font26());
  lv_obj_set_width(out_handles->home_date_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->home_date_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  out_handles->home_time_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->home_time_label, "--:-- --");
  style_label(out_handles->home_time_label, font30());
  lv_obj_set_width(out_handles->home_time_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->home_time_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  make_transparent(left_col, LV_PCT(100), 20);

  lv_obj_t *weather_obj = make_transparent(left_col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(weather_obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(weather_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(weather_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  out_handles->weather_label = lv_label_create(weather_obj);
  lv_label_set_text(out_handles->weather_label, "Sunny 38°");
  style_label(out_handles->weather_label, font26());
  lv_obj_set_width(out_handles->weather_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->weather_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  out_handles->weather_icon_label = lv_label_create(weather_obj);
  lv_label_set_text(out_handles->weather_icon_label, LV_SYMBOL_IMAGE);
  style_label(out_handles->weather_icon_label, LV_FONT_DEFAULT);
  lv_obj_set_size(out_handles->weather_icon_label, 50, 50);
  lv_obj_set_style_text_align(out_handles->weather_icon_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  make_transparent(left_col, LV_PCT(100), 40);

  lv_obj_t *status_title = lv_label_create(left_col);
  lv_label_set_text(status_title, "STATUS");
  style_label(status_title, font20());
  lv_obj_set_width(status_title, LV_PCT(100));
  lv_obj_set_style_text_align(status_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t status_line_pts[] = {{0, 0}, {160, 0}};
  lv_obj_t *status_line = lv_line_create(left_col);
  lv_line_set_points(status_line, status_line_pts, 2);
  lv_obj_set_style_line_width(status_line, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(status_line, lv_color_hex(kColorWhite), LV_PART_MAIN);

  out_handles->status_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->status_label, "Disconnected");
  style_label(out_handles->status_label, font30());
  lv_obj_set_width(out_handles->status_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->status_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  lv_obj_t *mid_col = make_transparent(home_root, 310, LV_PCT(100));
  lv_obj_set_layout(mid_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mid_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mid_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  lv_obj_t *indoor_title = lv_label_create(mid_col);
  lv_label_set_text(indoor_title, "INDOOR");
  style_label(indoor_title, font20());
  lv_obj_set_width(indoor_title, LV_PCT(100));
  lv_obj_set_style_text_align(indoor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  make_transparent(mid_col, LV_PCT(100), 20);

  out_handles->indoor_label = lv_label_create(mid_col);
  lv_label_set_text(out_handles->indoor_label, "N/A");
  style_indoor_temp_label(out_handles->indoor_label);
  lv_obj_set_width(out_handles->indoor_label, LV_PCT(100));
  lv_obj_set_height(out_handles->indoor_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(out_handles->indoor_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  make_transparent(mid_col, LV_PCT(100), 40);

  out_handles->humidity_label = lv_label_create(mid_col);
  lv_label_set_text(out_handles->humidity_label, "50% Humidity");
  style_label(out_handles->humidity_label, font30());
  lv_obj_set_width(out_handles->humidity_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->humidity_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  out_handles->setpoint_column = make_transparent(home_root, 90, LV_PCT(100));
  lv_obj_set_layout(out_handles->setpoint_column, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(out_handles->setpoint_column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(out_handles->setpoint_column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  make_transparent(out_handles->setpoint_column, 80, 20);

  lv_obj_t *btn_up = lv_btn_create(out_handles->setpoint_column);
  lv_obj_set_size(btn_up, 70, 70);
  style_primary_button(btn_up);
  if (callbacks.on_setpoint_up != nullptr) {
    lv_obj_add_event_cb(btn_up, callbacks.on_setpoint_up, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *up_label = lv_label_create(btn_up);
  lv_label_set_text(up_label, "^");
  style_label(up_label, font28());
  lv_obj_center(up_label);

  lv_obj_t *set_obj = make_transparent(out_handles->setpoint_column, 90, LV_SIZE_CONTENT);
  lv_obj_set_layout(set_obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(set_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(set_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  lv_obj_t *set_title = lv_label_create(set_obj);
  lv_label_set_text(set_title, "SET TO");
  style_label(set_title, font20());
  lv_obj_set_width(set_title, LV_PCT(100));
  lv_obj_set_style_text_align(set_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  out_handles->setpoint_label = lv_label_create(set_obj);
  lv_label_set_text(out_handles->setpoint_label, "68°");
  style_label(out_handles->setpoint_label, font40());
  lv_obj_set_width(out_handles->setpoint_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->setpoint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  lv_obj_t *btn_down = lv_btn_create(out_handles->setpoint_column);
  lv_obj_set_size(btn_down, 70, 70);
  style_primary_button(btn_down);
  if (callbacks.on_setpoint_down != nullptr) {
    lv_obj_add_event_cb(btn_down, callbacks.on_setpoint_down, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *down_label = lv_label_create(btn_down);
  lv_label_set_text(down_label, "v");
  style_label(down_label, font28());
  lv_obj_center(down_label);

  lv_obj_t *fan_root = make_transparent(out_handles->fan_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(fan_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(fan_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(fan_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(fan_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(fan_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_top(fan_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(fan_root, 10, LV_PART_MAIN);

  lv_obj_t *fan_matrix = make_transparent(fan_root, 360, 220);
  lv_obj_set_layout(fan_matrix, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(fan_matrix, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(fan_matrix, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(fan_matrix, 10, LV_PART_MAIN);

  lv_obj_t *fan_auto = lv_btn_create(fan_matrix);
  g_fan_auto_btn = fan_auto;
  lv_obj_set_size(fan_auto, 360, 66);
  lv_obj_add_style(fan_auto, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(fan_auto, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(fan_auto, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(fan_auto, fan_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::Automatic)));
  lv_obj_t *fan_auto_label = lv_label_create(fan_auto);
  lv_label_set_text(fan_auto_label, "Automatic");
  style_label(fan_auto_label, font20());

  lv_obj_t *fan_on = lv_btn_create(fan_matrix);
  g_fan_on_btn = fan_on;
  lv_obj_set_size(fan_on, 360, 66);
  lv_obj_add_style(fan_on, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(fan_on, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(fan_on, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(fan_on, fan_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::AlwaysOn)));
  lv_obj_t *fan_on_label = lv_label_create(fan_on);
  lv_label_set_text(fan_on_label, "Always On");
  style_label(fan_on_label, font20());

  lv_obj_t *fan_circ = lv_btn_create(fan_matrix);
  g_fan_circ_btn = fan_circ;
  lv_obj_set_size(fan_circ, 360, 66);
  lv_obj_add_style(fan_circ, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(fan_circ, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(fan_circ, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(fan_circ, fan_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::Circulate)));
  lv_obj_t *fan_circ_label = lv_label_create(fan_circ);
  lv_label_set_text(fan_circ_label, "Circulate");
  style_label(fan_circ_label, font20());
  apply_fan_state(FanMode::Automatic);

  lv_obj_t *mode_root = make_transparent(out_handles->mode_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(mode_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mode_root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mode_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(mode_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(mode_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_top(mode_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(mode_root, 10, LV_PART_MAIN);

  lv_obj_t *mode_matrix = make_transparent(mode_root, 360, 220);
  lv_obj_set_layout(mode_matrix, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mode_matrix, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mode_matrix, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(mode_matrix, 10, LV_PART_MAIN);

  lv_obj_t *mode_heat = lv_btn_create(mode_matrix);
  g_mode_heat_btn = mode_heat;
  lv_obj_set_size(mode_heat, 360, 66);
  lv_obj_add_style(mode_heat, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(mode_heat, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(mode_heat, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(mode_heat, mode_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Heat)));
  lv_obj_t *mode_heat_label = lv_label_create(mode_heat);
  lv_label_set_text(mode_heat_label, "Heat");
  style_label(mode_heat_label, font20());

  lv_obj_t *mode_cool = lv_btn_create(mode_matrix);
  g_mode_cool_btn = mode_cool;
  lv_obj_set_size(mode_cool, 360, 66);
  lv_obj_add_style(mode_cool, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(mode_cool, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(mode_cool, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(mode_cool, mode_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Cool)));
  lv_obj_t *mode_cool_label = lv_label_create(mode_cool);
  lv_label_set_text(mode_cool_label, "Cool");
  style_label(mode_cool_label, font20());

  lv_obj_t *mode_off = lv_btn_create(mode_matrix);
  g_mode_off_btn = mode_off;
  lv_obj_set_size(mode_off, 360, 66);
  lv_obj_add_style(mode_off, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(mode_off, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(mode_off, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(mode_off, mode_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Off)));
  lv_obj_t *mode_off_label = lv_label_create(mode_off);
  lv_label_set_text(mode_off_label, "Off");
  style_label(mode_off_label, font20());
  apply_mode_state(FurnaceMode::Off);

  lv_obj_t *settings_root = make_transparent(out_handles->settings_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(settings_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(settings_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(settings_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_left(settings_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(settings_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_top(settings_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(settings_root, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_column(settings_root, 20, LV_PART_MAIN);

  lv_obj_t *settings_controls = make_transparent(settings_root, 420, LV_PCT(100));
  lv_obj_set_layout(settings_controls, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(settings_controls, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settings_controls, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(settings_controls, 6, LV_PART_MAIN);

  lv_obj_t *temp_unit_title = lv_label_create(settings_controls);
  lv_label_set_text(temp_unit_title, "Temp Unit");
  style_label(temp_unit_title, font26());

  lv_obj_t *unit_row = make_transparent(settings_controls, 320, 56);
  lv_obj_set_layout(unit_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(unit_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(unit_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(unit_row, 8, LV_PART_MAIN);

  lv_obj_t *unit_f = lv_btn_create(unit_row);
  g_unit_f_btn = unit_f;
  lv_obj_set_size(unit_f, 156, 56);
  lv_obj_add_style(unit_f, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(unit_f, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(unit_f, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(unit_f, unit_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(TemperatureUnit::Fahrenheit)));
  lv_obj_t *unit_f_label = lv_label_create(unit_f);
  lv_label_set_text(unit_f_label, "F");
  style_label(unit_f_label, font20());

  lv_obj_t *unit_c = lv_btn_create(unit_row);
  g_unit_c_btn = unit_c;
  lv_obj_set_size(unit_c, 156, 56);
  lv_obj_add_style(unit_c, &g_style_header_item, LV_PART_MAIN);
  lv_obj_add_style(unit_c, &g_style_header_item_checked, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_add_flag(unit_c, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(unit_c, unit_button_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(TemperatureUnit::Celsius)));
  lv_obj_t *unit_c_label = lv_label_create(unit_c);
  lv_label_set_text(unit_c_label, "C");
  style_label(unit_c_label, font20());
  apply_temperature_unit_state(TemperatureUnit::Celsius);

  lv_obj_t *actions_title = lv_label_create(settings_controls);
  lv_label_set_text(actions_title, "Actions");
  style_label(actions_title, font26());

  lv_obj_t *actions_row = make_transparent(settings_controls, 320, 56);
  lv_obj_set_layout(actions_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(actions_row, 8, LV_PART_MAIN);

  lv_obj_t *sync_btn = lv_btn_create(actions_row);
  lv_obj_set_size(sync_btn, 156, 56);
  style_settings_button(sync_btn);
  if (callbacks.on_sync != nullptr) {
    lv_obj_add_event_cb(sync_btn, callbacks.on_sync, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *sync_label = lv_label_create(sync_btn);
  lv_label_set_text(sync_label, "Sync");
  style_label(sync_label, font20());

  lv_obj_t *filter_btn = lv_btn_create(actions_row);
  lv_obj_set_size(filter_btn, 156, 56);
  style_settings_button(filter_btn);
  if (callbacks.on_filter_reset != nullptr) {
    lv_obj_add_event_cb(filter_btn, callbacks.on_filter_reset, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *filter_label = lv_label_create(filter_btn);
  lv_label_set_text(filter_label, "Filter Reset");
  style_label(filter_label, font20());

  lv_obj_t *display_title = lv_label_create(settings_controls);
  lv_label_set_text(display_title, "Display");
  style_label(display_title, font26());

  lv_obj_t *timeout_label = lv_label_create(settings_controls);
  lv_label_set_text(timeout_label, "Display Timeout");
  style_label(timeout_label, font26());
  out_handles->timeout_slider = lv_slider_create(settings_controls);
  lv_obj_set_width(out_handles->timeout_slider, 320);
  lv_slider_set_range(out_handles->timeout_slider, 30, 600);
  lv_slider_set_value(out_handles->timeout_slider, 300, LV_ANIM_OFF);
  style_settings_slider(out_handles->timeout_slider);
  if (callbacks.on_timeout_slider != nullptr) {
    lv_obj_add_event_cb(out_handles->timeout_slider, callbacks.on_timeout_slider, LV_EVENT_RELEASED, nullptr);
  }

  lv_obj_t *brightness_label = lv_label_create(settings_controls);
  lv_label_set_text(brightness_label, "Brightness");
  style_label(brightness_label, font26());
  out_handles->brightness_slider = lv_slider_create(settings_controls);
  lv_obj_set_width(out_handles->brightness_slider, 320);
  lv_slider_set_range(out_handles->brightness_slider, 0, 100);
  lv_slider_set_value(out_handles->brightness_slider, 100, LV_ANIM_OFF);
  style_settings_slider(out_handles->brightness_slider);
  if (callbacks.on_brightness_slider != nullptr) {
    lv_obj_add_event_cb(out_handles->brightness_slider, callbacks.on_brightness_slider, LV_EVENT_RELEASED,
                        nullptr);
  }

  lv_obj_t *dim_label = lv_label_create(settings_controls);
  lv_label_set_text(dim_label, "Screensaver Brightness");
  style_label(dim_label, font26());
  out_handles->dim_slider = lv_slider_create(settings_controls);
  lv_obj_set_width(out_handles->dim_slider, 320);
  lv_slider_set_range(out_handles->dim_slider, 0, 100);
  lv_slider_set_value(out_handles->dim_slider, 16, LV_ANIM_OFF);
  style_settings_slider(out_handles->dim_slider);
  if (callbacks.on_dim_slider != nullptr) {
    lv_obj_add_event_cb(out_handles->dim_slider, callbacks.on_dim_slider, LV_EVENT_RELEASED, nullptr);
  }

  lv_obj_t *settings_network = make_transparent(settings_root, 320, LV_PCT(100));
  lv_obj_set_layout(settings_network, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(settings_network, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settings_network, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(settings_network, 6, LV_PART_MAIN);
  lv_obj_add_flag(settings_network, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(settings_network, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(settings_network, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *net_title = lv_label_create(settings_network);
  lv_label_set_text(net_title, "Network");
  style_label(net_title, font26());

  out_handles->settings_display_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_display_label, 320);
  lv_label_set_long_mode(out_handles->settings_display_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_display_label, font20());

  lv_obj_t *about_title = lv_label_create(settings_network);
  lv_label_set_text(about_title, "About");
  style_label(about_title, font26());

  out_handles->settings_diag_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_diag_label, 320);
  lv_label_set_long_mode(out_handles->settings_diag_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_diag_label, font20());

  lv_obj_t *screensaver_root = make_transparent(out_handles->screensaver_page, LV_PCT(100), LV_PCT(100));
  out_handles->screen_time_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(out_handles->screen_time_label, 40, 70);
  style_label(out_handles->screen_time_label, font48());

  lv_obj_t *screen_weather_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(screen_weather_label, 48, 156);
  lv_label_set_text(screen_weather_label, "Weather");
  style_label(screen_weather_label, font28());

  lv_obj_t *screen_indoor_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(screen_indoor_label, 32, 240);
  lv_label_set_text(screen_indoor_label, "Indoor");
  style_label(screen_indoor_label, font80());
}

void set_mode_button_state(FurnaceMode mode) { apply_mode_state(mode); }
void set_fan_button_state(FanMode mode) { apply_fan_state(mode); }
void set_temperature_unit_button_state(TemperatureUnit unit) { apply_temperature_unit_state(unit); }

}  // namespace ui
}  // namespace thermostat

#endif
