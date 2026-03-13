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
extern "C" const lv_font_t thermostat_font_mdi_weather_30;

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

static const char *s_confirm_btns[] = {"Yes", "Cancel", ""};
static lv_event_cb_t s_confirm_cb = nullptr;

void confirm_msgbox_cb(lv_event_t *e) {
  lv_obj_t *msgbox = lv_event_get_current_target(e);
  const char *btn_text = lv_msgbox_get_active_btn_text(msgbox);
  if (btn_text == nullptr) return;
  if (strcmp(btn_text, "Yes") == 0) {
    if (s_confirm_cb) s_confirm_cb(e);
  }
  s_confirm_cb = nullptr;
  lv_msgbox_close(msgbox);
}

void show_confirm_dialog(const char *title, const char *message, lv_event_cb_t on_confirm) {
  s_confirm_cb = on_confirm;
  lv_obj_t *msgbox = lv_msgbox_create(nullptr, title, message, s_confirm_btns, false);
  lv_obj_set_style_text_font(msgbox, &thermostat_font_montserrat_20, LV_PART_MAIN);
  lv_obj_center(msgbox);
  lv_obj_add_event_cb(msgbox, confirm_msgbox_cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

lv_event_cb_t g_wifi_reset_cb = nullptr;
lv_event_cb_t g_restart_cb = nullptr;

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
  lv_obj_set_style_text_align(button, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_layout(button, LV_LAYOUT_FLEX);
  lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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

struct Rect {
  lv_coord_t x;
  lv_coord_t y;
  lv_coord_t w;
  lv_coord_t h;
};

bool intersects(const Rect &a, const Rect &b) {
  const lv_coord_t ar = a.x + a.w;
  const lv_coord_t ab = a.y + a.h;
  const lv_coord_t br = b.x + b.w;
  const lv_coord_t bb = b.y + b.h;
  return a.x < br && ar > b.x && a.y < bb && ab > b.y;
}

bool fits(const Rect &r, lv_coord_t max_w, lv_coord_t max_h) {
  return r.x >= 0 && r.y >= 0 && (r.x + r.w) <= max_w && (r.y + r.h) <= max_h;
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

  lv_obj_set_style_bg_color(out_handles->home_page, lv_color_hex(0x0A1B2A), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(out_handles->home_page, lv_color_hex(0x11506B), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(out_handles->home_page, LV_GRAD_DIR_HOR, LV_PART_MAIN);

  lv_obj_t *home_root = make_transparent(out_handles->home_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(home_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(home_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(home_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_left(home_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_right(home_root, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_top(home_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(home_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_column(home_root, 16, LV_PART_MAIN);

  lv_obj_t *left_col = make_transparent(home_root, 300, LV_PCT(100));
  lv_obj_set_layout(left_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(left_col, 6, LV_PART_MAIN);

  out_handles->home_date_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->home_date_label, "--- date ---");
  style_label(out_handles->home_date_label, font26());
  lv_obj_set_width(out_handles->home_date_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->home_date_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  out_handles->home_time_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->home_time_label, "--:-- --");
  style_label(out_handles->home_time_label, font48());
  lv_obj_set_width(out_handles->home_time_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->home_time_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  lv_obj_t *outdoor_section = make_transparent(left_col, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(outdoor_section, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(outdoor_section, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(outdoor_section, 4, LV_PART_MAIN);
  out_handles->outdoor_section = outdoor_section;
  lv_obj_add_flag(outdoor_section, LV_OBJ_FLAG_HIDDEN);  // hidden until weather data arrives

  make_transparent(outdoor_section, LV_PCT(100), 14);

  lv_obj_t *outdoor_title = lv_label_create(outdoor_section);
  lv_label_set_text(outdoor_title, "OUTDOOR");
  style_label(outdoor_title, font20());
  lv_obj_set_width(outdoor_title, LV_PCT(100));
  lv_obj_set_style_text_align(outdoor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t outdoor_line_pts[] = {{0, 0}, {286, 0}};
  lv_obj_t *outdoor_line = lv_line_create(outdoor_section);
  lv_line_set_points(outdoor_line, outdoor_line_pts, 2);
  lv_obj_set_style_line_width(outdoor_line, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(outdoor_line, lv_color_hex(kColorWhite), LV_PART_MAIN);
  lv_obj_set_style_line_opa(outdoor_line, LV_OPA_70, LV_PART_MAIN);

  lv_obj_t *weather_row = make_transparent(outdoor_section, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_layout(weather_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(weather_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(weather_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(weather_row, 8, LV_PART_MAIN);

  out_handles->weather_icon_label = lv_label_create(weather_row);
  lv_label_set_text(out_handles->weather_icon_label, LV_SYMBOL_IMAGE);
  style_label(out_handles->weather_icon_label, font_mdi_weather_30());
  lv_obj_set_style_text_align(out_handles->weather_icon_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  out_handles->weather_label = lv_label_create(weather_row);
  lv_label_set_text(out_handles->weather_label, "---");
  style_label(out_handles->weather_label, font30());
  lv_obj_set_width(out_handles->weather_label, 260);
  lv_obj_set_style_text_align(out_handles->weather_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  make_transparent(left_col, LV_PCT(100), 30);

  lv_obj_t *status_title = lv_label_create(left_col);
  lv_label_set_text(status_title, "STATUS");
  style_label(status_title, font20());
  lv_obj_set_width(status_title, LV_PCT(100));
  lv_obj_set_style_text_align(status_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t status_line_pts[] = {{0, 0}, {286, 0}};
  lv_obj_t *status_line = lv_line_create(left_col);
  lv_line_set_points(status_line, status_line_pts, 2);
  lv_obj_set_style_line_width(status_line, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(status_line, lv_color_hex(kColorWhite), LV_PART_MAIN);
  lv_obj_set_style_line_opa(status_line, LV_OPA_70, LV_PART_MAIN);

  out_handles->status_label = lv_label_create(left_col);
  lv_label_set_text(out_handles->status_label, "---");
  style_label(out_handles->status_label, font30());
  lv_obj_set_width(out_handles->status_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->status_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t center_divider_pts[] = {{0, 0}, {0, 330}};
  lv_obj_t *center_divider = lv_line_create(home_root);
  lv_line_set_points(center_divider, center_divider_pts, 2);
  lv_obj_set_style_line_width(center_divider, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(center_divider, lv_color_hex(kColorWhite), LV_PART_MAIN);
  lv_obj_set_style_line_opa(center_divider, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_pad_top(center_divider, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(center_divider, 20, LV_PART_MAIN);

  lv_obj_t *mid_col = make_transparent(home_root, 280, LV_PCT(100));
  lv_obj_set_layout(mid_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mid_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mid_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(mid_col, 8, LV_PART_MAIN);

  lv_obj_t *indoor_title = lv_label_create(mid_col);
  lv_label_set_text(indoor_title, "INDOOR");
  style_label(indoor_title, font20());
  lv_obj_set_width(indoor_title, LV_PCT(100));
  lv_obj_set_style_text_align(indoor_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  out_handles->indoor_label = lv_label_create(mid_col);
  lv_label_set_text(out_handles->indoor_label, "---");
  style_indoor_temp_label(out_handles->indoor_label);
  lv_obj_set_width(out_handles->indoor_label, LV_PCT(100));
  lv_obj_set_height(out_handles->indoor_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(out_handles->indoor_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  out_handles->humidity_label = lv_label_create(mid_col);
  lv_label_set_text(out_handles->humidity_label, "---");
  style_label(out_handles->humidity_label, font30());
  lv_obj_set_width(out_handles->humidity_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->humidity_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  out_handles->filter_label = lv_label_create(mid_col);
  lv_label_set_text(out_handles->filter_label, "");
  style_label(out_handles->filter_label, font30());
  lv_obj_set_width(out_handles->filter_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->filter_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(out_handles->filter_label, lv_color_hex(0xFF6600), LV_PART_MAIN);
  lv_obj_add_flag(out_handles->filter_label, LV_OBJ_FLAG_HIDDEN);

  out_handles->setpoint_column = make_transparent(home_root, 130, LV_PCT(100));
  lv_obj_set_layout(out_handles->setpoint_column, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(out_handles->setpoint_column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(out_handles->setpoint_column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(out_handles->setpoint_column, lv_color_hex(0x0E2431), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(out_handles->setpoint_column, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_width(out_handles->setpoint_column, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(out_handles->setpoint_column, lv_color_hex(0x1F3F52), LV_PART_MAIN);
  lv_obj_set_style_radius(out_handles->setpoint_column, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_top(out_handles->setpoint_column, 16, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(out_handles->setpoint_column, 16, LV_PART_MAIN);

  lv_obj_t *btn_up = lv_btn_create(out_handles->setpoint_column);
  lv_obj_set_size(btn_up, 86, 86);
  style_primary_button(btn_up);
  lv_obj_set_style_radius(btn_up, 43, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x2E83B1), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(btn_up, lv_color_hex(0x0C3C59), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(btn_up, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_up, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(btn_up, lv_color_hex(0x0A2737), LV_PART_MAIN);
  if (callbacks.on_setpoint_up != nullptr) {
    lv_obj_add_event_cb(btn_up, callbacks.on_setpoint_up, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *up_label = lv_label_create(btn_up);
  lv_label_set_text(up_label, "^");
  style_label(up_label, font28());
  lv_obj_center(up_label);

  lv_obj_t *set_obj = make_transparent(out_handles->setpoint_column, 120, LV_SIZE_CONTENT);
  lv_obj_set_layout(set_obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(set_obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(set_obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *set_title = lv_label_create(set_obj);
  lv_label_set_text(set_title, "SET TO");
  style_label(set_title, font20());
  lv_obj_set_width(set_title, LV_PCT(100));
  lv_obj_set_style_text_align(set_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  out_handles->setpoint_label = lv_label_create(set_obj);
  lv_label_set_text(out_handles->setpoint_label, "--");
  lv_label_set_long_mode(out_handles->setpoint_label, LV_LABEL_LONG_CLIP);
  style_label(out_handles->setpoint_label, font40());
  lv_obj_set_width(out_handles->setpoint_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(out_handles->setpoint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  lv_obj_t *btn_down = lv_btn_create(out_handles->setpoint_column);
  lv_obj_set_size(btn_down, 86, 86);
  style_primary_button(btn_down);
  lv_obj_set_style_radius(btn_down, 43, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn_down, lv_color_hex(0x2E83B1), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(btn_down, lv_color_hex(0x0C3C59), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(btn_down, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_down, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(btn_down, lv_color_hex(0x0A2737), LV_PART_MAIN);
  if (callbacks.on_setpoint_down != nullptr) {
    lv_obj_add_event_cb(btn_down, callbacks.on_setpoint_down, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t *down_label = lv_label_create(btn_down);
  lv_label_set_text(down_label, "v");
  style_label(down_label, font28());
  lv_obj_center(down_label);

  lv_obj_t *fan_root = make_transparent(out_handles->fan_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_layout(fan_root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(fan_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fan_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(fan_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_right(fan_root, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_top(fan_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(fan_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_column(fan_root, 40, LV_PART_MAIN);

  lv_obj_t *fan_left_col = make_transparent(fan_root, 300, LV_PCT(100));
  lv_obj_set_layout(fan_left_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(fan_left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(fan_left_col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(fan_left_col, 6, LV_PART_MAIN);

  lv_obj_t *fan_status_title = lv_label_create(fan_left_col);
  lv_label_set_text(fan_status_title, "STATUS");
  style_label(fan_status_title, font20());
  lv_obj_set_width(fan_status_title, LV_PCT(100));
  lv_obj_set_style_text_align(fan_status_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t fan_status_line_pts[] = {{0, 0}, {286, 0}};
  lv_obj_t *fan_status_line = lv_line_create(fan_left_col);
  lv_line_set_points(fan_status_line, fan_status_line_pts, 2);
  lv_obj_set_style_line_width(fan_status_line, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(fan_status_line, lv_color_hex(kColorWhite), LV_PART_MAIN);
  lv_obj_set_style_line_opa(fan_status_line, LV_OPA_70, LV_PART_MAIN);

  out_handles->fan_status_label = lv_label_create(fan_left_col);
  lv_label_set_text(out_handles->fan_status_label, "---");
  style_label(out_handles->fan_status_label, font30());
  lv_obj_set_width(out_handles->fan_status_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->fan_status_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

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
  lv_obj_set_flex_flow(mode_root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mode_root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(mode_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_right(mode_root, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_top(mode_root, 26, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(mode_root, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_column(mode_root, 40, LV_PART_MAIN);

  lv_obj_t *mode_left_col = make_transparent(mode_root, 300, LV_PCT(100));
  lv_obj_set_layout(mode_left_col, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mode_left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mode_left_col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(mode_left_col, 6, LV_PART_MAIN);

  lv_obj_t *mode_status_title = lv_label_create(mode_left_col);
  lv_label_set_text(mode_status_title, "STATUS");
  style_label(mode_status_title, font20());
  lv_obj_set_width(mode_status_title, LV_PCT(100));
  lv_obj_set_style_text_align(mode_status_title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  static lv_point_t mode_status_line_pts[] = {{0, 0}, {286, 0}};
  lv_obj_t *mode_status_line = lv_line_create(mode_left_col);
  lv_line_set_points(mode_status_line, mode_status_line_pts, 2);
  lv_obj_set_style_line_width(mode_status_line, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(mode_status_line, lv_color_hex(kColorWhite), LV_PART_MAIN);
  lv_obj_set_style_line_opa(mode_status_line, LV_OPA_70, LV_PART_MAIN);

  out_handles->mode_status_label = lv_label_create(mode_left_col);
  lv_label_set_text(out_handles->mode_status_label, "---");
  style_label(out_handles->mode_status_label, font30());
  lv_obj_set_width(out_handles->mode_status_label, LV_PCT(100));
  lv_obj_set_style_text_align(out_handles->mode_status_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

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
  lv_obj_add_flag(settings_controls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(settings_controls, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(settings_controls, LV_SCROLLBAR_MODE_AUTO);

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

  lv_obj_t *actions_row2 = make_transparent(settings_controls, 320, 56);
  lv_obj_set_layout(actions_row2, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(actions_row2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(actions_row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(actions_row2, 8, LV_PART_MAIN);

  g_wifi_reset_cb = callbacks.on_wifi_reset;
  lv_obj_t *wifi_reset_btn = lv_btn_create(actions_row2);
  lv_obj_set_size(wifi_reset_btn, 156, 56);
  style_settings_button(wifi_reset_btn);
  lv_obj_add_event_cb(wifi_reset_btn, [](lv_event_t *) {
    show_confirm_dialog("Reset WiFi",
                        "Clear WiFi credentials and reboot into provisioning mode?",
                        g_wifi_reset_cb);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *wifi_reset_label = lv_label_create(wifi_reset_btn);
  lv_label_set_text(wifi_reset_label, "Reset WiFi");
  style_label(wifi_reset_label, font20());

  g_restart_cb = callbacks.on_restart;
  lv_obj_t *restart_btn = lv_btn_create(actions_row2);
  lv_obj_set_size(restart_btn, 156, 56);
  style_settings_button(restart_btn);
  lv_obj_add_event_cb(restart_btn, [](lv_event_t *) {
    show_confirm_dialog("Restart", "Restart the display?", g_restart_cb);
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *restart_label = lv_label_create(restart_btn);
  lv_label_set_text(restart_label, "Restart");
  style_label(restart_label, font20());

  lv_obj_t *display_title = lv_label_create(settings_controls);
  lv_label_set_text(display_title, "Display");
  style_label(display_title, font26());

  lv_obj_t *timeout_label = lv_label_create(settings_controls);
  lv_label_set_text(timeout_label, "Display Timeout");
  style_label(timeout_label, font20());
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
  style_label(brightness_label, font20());
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
  style_label(dim_label, font20());
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

  lv_obj_t *system_title = lv_label_create(settings_network);
  lv_label_set_text(system_title, "System");
  style_label(system_title, font26());
  out_handles->settings_system_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_system_label, 320);
  lv_label_set_long_mode(out_handles->settings_system_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_system_label, LV_FONT_DEFAULT);

  lv_obj_t *wifi_title = lv_label_create(settings_network);
  lv_label_set_text(wifi_title, "WiFi");
  style_label(wifi_title, font26());
  out_handles->settings_wifi_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_wifi_label, 320);
  lv_label_set_long_mode(out_handles->settings_wifi_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_wifi_label, LV_FONT_DEFAULT);

  lv_obj_t *mqtt_title = lv_label_create(settings_network);
  lv_label_set_text(mqtt_title, "MQTT");
  style_label(mqtt_title, font26());
  out_handles->settings_mqtt_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_mqtt_label, 320);
  lv_label_set_long_mode(out_handles->settings_mqtt_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_mqtt_label, LV_FONT_DEFAULT);

  lv_obj_t *controller_title = lv_label_create(settings_network);
  lv_label_set_text(controller_title, "Controller");
  style_label(controller_title, font26());
  out_handles->settings_controller_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_controller_label, 320);
  lv_label_set_long_mode(out_handles->settings_controller_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_controller_label, LV_FONT_DEFAULT);

  lv_obj_t *espnow_title = lv_label_create(settings_network);
  lv_label_set_text(espnow_title, "ESP-NOW");
  style_label(espnow_title, font26());
  out_handles->settings_espnow_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_espnow_label, 320);
  lv_label_set_long_mode(out_handles->settings_espnow_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_espnow_label, LV_FONT_DEFAULT);

  lv_obj_t *config_title = lv_label_create(settings_network);
  lv_label_set_text(config_title, "Config");
  style_label(config_title, font26());
  out_handles->settings_config_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_config_label, 320);
  lv_label_set_long_mode(out_handles->settings_config_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_config_label, LV_FONT_DEFAULT);

  lv_obj_t *errors_title = lv_label_create(settings_network);
  lv_label_set_text(errors_title, "Errors");
  style_label(errors_title, font26());
  out_handles->settings_errors_label = lv_label_create(settings_network);
  lv_obj_set_width(out_handles->settings_errors_label, 320);
  lv_label_set_long_mode(out_handles->settings_errors_label, LV_LABEL_LONG_WRAP);
  style_label(out_handles->settings_errors_label, LV_FONT_DEFAULT);

  // Backward-compatible handles used by existing runtime/sim code.
  out_handles->settings_display_label = out_handles->settings_system_label;
  out_handles->settings_diag_label = out_handles->settings_errors_label;

  lv_obj_t *screensaver_root = make_transparent(out_handles->screensaver_page, LV_PCT(100), LV_PCT(100));
  out_handles->screen_time_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(out_handles->screen_time_label, 40, 70);
  style_label(out_handles->screen_time_label, font80());

  out_handles->screen_weather_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(out_handles->screen_weather_label, 48, 156);
  lv_label_set_text(out_handles->screen_weather_label, "--");
  style_label(out_handles->screen_weather_label, font40());

  out_handles->screen_indoor_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(out_handles->screen_indoor_label, 32, 240);
  lv_label_set_text(out_handles->screen_indoor_label, "--");
  style_label(out_handles->screen_indoor_label, font120());

  out_handles->screen_status_label = lv_label_create(screensaver_root);
  lv_obj_set_pos(out_handles->screen_status_label, 48, 200);
  lv_label_set_text(out_handles->screen_status_label, "");
  style_label(out_handles->screen_status_label, font40());
}

void set_mode_button_state(FurnaceMode mode) { apply_mode_state(mode); }
void set_fan_button_state(FanMode mode) { apply_fan_state(mode); }
void set_temperature_unit_button_state(TemperatureUnit unit) { apply_temperature_unit_state(unit); }

void set_fan_mode_buttons_enabled(bool enabled) {
  lv_obj_t *btns[] = {g_fan_auto_btn, g_fan_on_btn, g_fan_circ_btn};
  for (auto *btn : btns) {
    if (btn == nullptr) continue;
    if (enabled) {
      lv_obj_clear_state(btn, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
  }
}

void set_mode_buttons_enabled(bool enabled) {
  lv_obj_t *btns[] = {g_mode_heat_btn, g_mode_cool_btn, g_mode_off_btn};
  for (auto *btn : btns) {
    if (btn == nullptr) continue;
    if (enabled) {
      lv_obj_clear_state(btn, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
    }
  }
}

const lv_font_t *font_mdi_weather_30() { return &thermostat_font_mdi_weather_30; }

const char *weather_icon_symbol(WeatherIcon icon) {
  // MDI codepoints encoded as UTF-8.  Each Supplementary Plane codepoint
  // (U+F0xxx) encodes to a 4-byte UTF-8 sequence.
  switch (icon) {
    case WeatherIcon::Sunny:        return "\xF3\xB0\x96\x99";  // U+F0599 weather-sunny
    case WeatherIcon::PartlyCloudy: return "\xF3\xB0\x96\x95";  // U+F0595 weather-partly-cloudy
    case WeatherIcon::Cloudy:       return "\xF3\xB0\x96\x90";  // U+F0590 weather-cloudy
    case WeatherIcon::Rain:         return "\xF3\xB0\x96\x97";  // U+F0597 weather-rainy
    case WeatherIcon::RainHeavy:    return "\xF3\xB0\x96\x96";  // U+F0596 weather-pouring
    case WeatherIcon::RainLight:    return "\xF3\xB0\x96\x97";  // U+F0597 weather-rainy
    case WeatherIcon::Lightning:    return "\xF3\xB0\x96\x93";  // U+F0593 weather-lightning
    case WeatherIcon::Snow:         return "\xF3\xB0\x96\x98";  // U+F0598 weather-snowy
    case WeatherIcon::SnowLight:    return "\xF3\xB0\x96\x98";  // U+F0598 weather-snowy
    case WeatherIcon::Sleet:        return "\xF3\xB0\x96\x98";  // U+F0598 weather-snowy
    case WeatherIcon::Hail:         return "\xF3\xB0\x96\x92";  // U+F0592 weather-hail
    case WeatherIcon::Windy:        return "\xF3\xB0\x96\x9D";  // U+F059D weather-windy
    case WeatherIcon::Fog:          return "\xF3\xB0\x96\x91";  // U+F0591 weather-fog
    case WeatherIcon::Haze:         return "\xF3\xB0\x96\x91";  // U+F0591 weather-fog
    case WeatherIcon::Dust:         return "\xF3\xB0\x96\x9D";  // U+F059D weather-windy
    case WeatherIcon::Dry:          return "\xF3\xB0\x96\x99";  // U+F0599 weather-sunny
    case WeatherIcon::Night:        return "\xF3\xB0\x96\x94";  // U+F0594 weather-night
    case WeatherIcon::NightCloudy:  return "\xF3\xB0\x96\x94";  // U+F0594 weather-night
    case WeatherIcon::Unknown:      return "\xF3\xB0\x96\x90";  // U+F0590 weather-cloudy
  }
  return "\xF3\xB0\x96\x90";  // fallback: cloudy
}

void update_screensaver_layout(lv_obj_t *time_label, lv_obj_t *weather_label, lv_obj_t *indoor_label,
                               lv_obj_t *status_label, uint32_t minute_index) {
  if (time_label == nullptr || weather_label == nullptr || indoor_label == nullptr) {
    return;
  }

  lv_obj_update_layout(time_label);
  lv_obj_update_layout(weather_label);
  lv_obj_update_layout(indoor_label);

  const lv_coord_t time_w = lv_obj_get_width(time_label);
  const lv_coord_t time_h = lv_obj_get_height(time_label);
  const lv_coord_t weather_w = lv_obj_get_width(weather_label);
  const lv_coord_t weather_h = lv_obj_get_height(weather_label);
  const lv_coord_t indoor_w = lv_obj_get_width(indoor_label);
  const lv_coord_t indoor_h = lv_obj_get_height(indoor_label);

  const lv_coord_t page_w = kDisplayWidth;
  const lv_coord_t page_h = kDisplayHeight - 50;
  const lv_coord_t margin = 24;

  struct LayoutPos {
    int time_x;
    int time_y;
    int weather_x;
    int weather_y;
    int indoor_x;
    int indoor_y;
  };

  const LayoutPos candidates[] = {
      {margin, margin, margin, 120, page_w - margin - indoor_w, page_h - margin - indoor_h},
      {page_w - margin - time_w, margin, margin, page_h - margin - weather_h, margin,
       page_h - margin - indoor_h},
      {margin, page_h - margin - time_h, page_w - margin - weather_w, margin, margin, 126},
      {page_w - margin - time_w, page_h - margin - time_h, margin, margin, page_w - margin - indoor_w, 126},
      {margin, 96, page_w - margin - weather_w, page_h - margin - weather_h, margin,
       page_h - margin - indoor_h},
      {page_w - margin - time_w, 96, margin, page_h - margin - weather_h, page_w - margin - indoor_w,
       page_h - margin - indoor_h},
  };

  const size_t count = sizeof(candidates) / sizeof(candidates[0]);
  const size_t start = static_cast<size_t>(minute_index % count);

  for (size_t i = 0; i < count; ++i) {
    const LayoutPos &c = candidates[(start + i) % count];
    const Rect time_r{static_cast<lv_coord_t>(c.time_x), static_cast<lv_coord_t>(c.time_y), time_w, time_h};
    const Rect weather_r{static_cast<lv_coord_t>(c.weather_x), static_cast<lv_coord_t>(c.weather_y), weather_w,
                         weather_h};
    const Rect indoor_r{static_cast<lv_coord_t>(c.indoor_x), static_cast<lv_coord_t>(c.indoor_y), indoor_w,
                        indoor_h};

    if (!fits(time_r, page_w, page_h) || !fits(weather_r, page_w, page_h) ||
        !fits(indoor_r, page_w, page_h)) {
      continue;
    }
    if (intersects(time_r, weather_r) || intersects(time_r, indoor_r) ||
        intersects(weather_r, indoor_r)) {
      continue;
    }

    lv_obj_set_pos(time_label, static_cast<lv_coord_t>(c.time_x), static_cast<lv_coord_t>(c.time_y));
    lv_obj_set_pos(weather_label, static_cast<lv_coord_t>(c.weather_x), static_cast<lv_coord_t>(c.weather_y));
    lv_obj_set_pos(indoor_label, static_cast<lv_coord_t>(c.indoor_x), static_cast<lv_coord_t>(c.indoor_y));
    if (status_label != nullptr) {
      lv_obj_set_pos(status_label, static_cast<lv_coord_t>(c.weather_x),
                     static_cast<lv_coord_t>(c.weather_y) + weather_h + 4);
    }
    return;
  }

  lv_obj_set_pos(time_label, margin, margin);
  lv_obj_set_pos(weather_label, margin, margin + time_h + 20);
  lv_obj_set_pos(indoor_label, margin, margin + time_h + weather_h + 40);
  if (status_label != nullptr) {
    lv_obj_set_pos(status_label, margin, margin + time_h + weather_h + 28);
  }
}

}  // namespace ui
}  // namespace thermostat

#endif
