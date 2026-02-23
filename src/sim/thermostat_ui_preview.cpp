#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include "thermostat/display_model.h"
#include "thermostat/thermostat_ui_state.h"
#include "thermostat/ui/thermostat_ui_shared.h"

namespace {

constexpr int kDisplayWidth = 800;
constexpr int kDisplayHeight = 480;
constexpr uint32_t kColorPageBg = 0x111827;
constexpr uint32_t kColorHeaderGrad = 0x005782;
constexpr uint32_t kColorHeaderBorder = 0x0077B3;
constexpr uint32_t kColorActionBtn = 0x4F46E5;
constexpr uint32_t kColorWhite = 0xFFFFFF;
constexpr uint32_t kColorBlack = 0x000000;

SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_texture = nullptr;
std::vector<uint32_t> g_framebuffer;
bool g_running = true;
uint32_t g_last_tick_ms = 0;
std::string g_capture_dir;
bool g_capture_mode = false;

lv_disp_draw_buf_t g_draw_buf;
std::vector<lv_color_t> g_lv_buf1;
std::vector<lv_color_t> g_lv_buf2;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;

lv_style_t g_style_page;
lv_style_t g_style_header_item;
lv_style_t g_style_primary_button;
lv_style_t g_style_text_white;
bool g_styles_ready = false;

struct MouseState {
  int x = 0;
  int y = 0;
  bool pressed = false;
} g_mouse;

lv_obj_t *g_home_page = nullptr;
lv_obj_t *g_fan_page = nullptr;
lv_obj_t *g_mode_page = nullptr;
lv_obj_t *g_settings_page = nullptr;
lv_obj_t *g_screensaver_page = nullptr;
lv_obj_t *g_tabs = nullptr;

lv_obj_t *g_home_date_label = nullptr;
lv_obj_t *g_home_time_label = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_indoor_label = nullptr;
lv_obj_t *g_humidity_label = nullptr;
lv_obj_t *g_setpoint_label = nullptr;
lv_obj_t *g_weather_label = nullptr;
lv_obj_t *g_weather_icon_label = nullptr;
lv_obj_t *g_screen_time_label = nullptr;
lv_obj_t *g_settings_diag_label = nullptr;
lv_obj_t *g_settings_display_label = nullptr;
lv_obj_t *g_timeout_slider = nullptr;
lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_dim_slider = nullptr;

enum class PreviewPage : uint8_t {
  Home = 0,
  Fan = 1,
  Mode = 2,
  Settings = 3,
  Screensaver = 4,
};

PreviewPage g_current_page = PreviewPage::Home;
float g_setpoint_c = 21.5f;
uint32_t g_display_timeout_s = 300;
uint8_t g_brightness_pct = 100;
uint8_t g_screensaver_brightness_pct = 16;
thermostat::DisplayModel g_preview_model;
thermostat::TemperatureUnit g_preview_unit = thermostat::TemperatureUnit::Celsius;
float g_preview_indoor_c = 22.2f;
float g_preview_humidity = 43.0f;
float g_preview_outdoor_c = 9.0f;
const char *g_preview_weather_condition = "Cloudy";

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
  lv_style_set_text_font(&g_style_header_item, LV_FONT_DEFAULT);

  lv_style_init(&g_style_primary_button);
  lv_style_set_bg_color(&g_style_primary_button, lv_color_hex(kColorActionBtn));
  lv_style_set_bg_opa(&g_style_primary_button, LV_OPA_COVER);
  lv_style_set_border_width(&g_style_primary_button, 0);
  lv_style_set_radius(&g_style_primary_button, 8);
  lv_style_set_text_color(&g_style_primary_button, lv_color_hex(kColorWhite));

  lv_style_init(&g_style_text_white);
  lv_style_set_text_color(&g_style_text_white, lv_color_hex(kColorWhite));
}

void style_label(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_add_style(label, &g_style_text_white, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
}

void style_primary_button(lv_obj_t *button) {
  lv_obj_add_style(button, &g_style_primary_button, LV_PART_MAIN);
}

uint32_t rgb565_to_argb8888(uint16_t px) {
  const uint8_t r = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
  const uint8_t g = static_cast<uint8_t>(((px >> 5) & 0x3F) * 255 / 63);
  const uint8_t b = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
  return 0xFF000000U | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  for (int y = area->y1; y <= area->y2; ++y) {
    const int row_base = y * kDisplayWidth;
    for (int x = area->x1; x <= area->x2; ++x) {
      const size_t src_idx = static_cast<size_t>((y - area->y1) * (area->x2 - area->x1 + 1) +
                                                 (x - area->x1));
      g_framebuffer[static_cast<size_t>(row_base + x)] =
          rgb565_to_argb8888(color_p[src_idx].full);
    }
  }
  lv_disp_flush_ready(disp_drv);
}

void mouse_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  data->state = g_mouse.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = g_mouse.x;
  data->point.y = g_mouse.y;
}

void show_page(PreviewPage page) {
  g_current_page = page;
  lv_obj_add_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);

  switch (page) {
    case PreviewPage::Home:
      lv_obj_clear_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case PreviewPage::Fan:
      lv_obj_clear_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case PreviewPage::Mode:
      lv_obj_clear_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case PreviewPage::Settings:
      lv_obj_clear_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case PreviewPage::Screensaver:
      lv_obj_clear_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);
      break;
  }
}

void update_labels() {
  const char *weather_icon = LV_SYMBOL_IMAGE;
  g_preview_model.set_temperature_unit(g_preview_unit);
  g_preview_model.set_local_setpoint_c(g_setpoint_c);
  g_preview_model.set_local_indoor_temperature_c(g_preview_indoor_c);
  g_preview_model.set_local_indoor_humidity(g_preview_humidity);
  g_preview_model.set_outdoor_temperature_c(g_preview_outdoor_c);
  g_preview_model.set_weather_condition(g_preview_weather_condition);

  const std::string setpoint_text = g_preview_model.format_setpoint_text();
  const std::string indoor_text = g_preview_model.format_indoor_temperature_text();
  const std::string humidity_text = g_preview_model.format_indoor_humidity_text();
  const std::string weather_text = g_preview_model.format_weather_text();
  const std::string status_text =
      thermostat::furnace_state_text(FurnaceStateCode::CoolMode, true, false, false);

  if (g_home_date_label != nullptr) {
    lv_label_set_text(g_home_date_label, "Sunday, Feb 22");
  }
  if (g_home_time_label != nullptr) {
    lv_label_set_text(g_home_time_label, "12:34 PM");
  }
  lv_label_set_text(g_setpoint_label, setpoint_text.c_str());
  lv_label_set_text(g_status_label, status_text.c_str());
  lv_label_set_text(g_indoor_label, indoor_text.c_str());
  lv_label_set_text(g_humidity_label, humidity_text.c_str());
  lv_label_set_text(g_weather_label, weather_text.c_str());
  if (g_weather_icon_label != nullptr) {
    lv_label_set_text(g_weather_icon_label, weather_icon);
  }
  lv_label_set_text(g_screen_time_label, "12:34 PM");

  char display_cfg[512];
  snprintf(display_cfg, sizeof(display_cfg),
           "SIM DATA (Preview)\n"
           "IP Address: 192.168.1.77\n"
           "MAC Address: AA:BB:CC:DD:EE:FF\n"
           "WiFi SSID: lab-2g\n"
           "WiFi Channel: 6\n"
           "WiFi RSSI: -58 dBm\n"
           "MQTT Host: mqtt.lan:1883\n"
           "MQTT Base Topic: thermostat/furnace-display");
  lv_label_set_text(g_settings_display_label, display_cfg);
  if (g_timeout_slider != nullptr && !lv_obj_has_state(g_timeout_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_timeout_slider, static_cast<int32_t>(g_display_timeout_s), LV_ANIM_OFF);
  }
  if (g_brightness_slider != nullptr && !lv_obj_has_state(g_brightness_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_brightness_slider, static_cast<int32_t>(g_brightness_pct), LV_ANIM_OFF);
  }
  if (g_dim_slider != nullptr && !lv_obj_has_state(g_dim_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_dim_slider, static_cast<int32_t>(g_screensaver_brightness_pct),
                        LV_ANIM_OFF);
  }

  char diag[1024];
  snprintf(diag, sizeof(diag),
           "SIM DATA (Preview)\n"
           "Version: 0.9.0-preview+18\n"
           "Build: native-ui-preview\n"
           "Boot Count: 14   Reset: software\n"
           "Uptime: %lus\n"
           "Temp Unit: %s   Temp Comp: -0.3 C\n"
           "Display Timeout: %lus\n"
           "Brightness: %u%%   Screensaver: %u%%\n"
           "Controller Timeout: 30000 ms\n"
           "MQTT Connected: yes   MQTT State: 0\n"
           "MQTT Client: esp32-furnace-thermostat\n"
           "ESP-NOW Channel: 6\n"
           "ESP-NOW Peer: 24:6F:28:AA:BB:CC\n"
           "ESP-NOW TX OK/Fail: 122/1\n"
           "Last MQTT Cmd: 7s ago\n"
           "Last Controller HB: 2s ago\n"
           "Raw Indoor Label: %s",
           static_cast<unsigned long>(SDL_GetTicks() / 1000U),
           g_preview_unit == thermostat::TemperatureUnit::Fahrenheit ? "F" : "C",
           static_cast<unsigned long>(g_display_timeout_s),
           static_cast<unsigned>(g_brightness_pct),
           static_cast<unsigned>(g_screensaver_brightness_pct),
           indoor_text.c_str());
  lv_label_set_text(g_settings_diag_label, diag);
}

void on_tab_changed(lv_event_t *e) {
  lv_obj_t *btnm = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(btnm, lv_btnmatrix_get_selected_btn(btnm));
  if (txt == nullptr) {
    return;
  }
  if (strcmp(txt, "HOME") == 0) {
    show_page(PreviewPage::Home);
  } else if (strcmp(txt, "FAN") == 0) {
    show_page(PreviewPage::Fan);
  } else if (strcmp(txt, "MODE") == 0) {
    show_page(PreviewPage::Mode);
  } else if (strcmp(txt, "SETTINGS") == 0) {
    show_page(PreviewPage::Settings);
  }
}

void on_setpoint_up(lv_event_t *) {
  const float step = (g_preview_unit == thermostat::TemperatureUnit::Fahrenheit) ? 1.0f : 0.5f;
  const float user_val = g_preview_model.to_user_temperature(g_setpoint_c);
  g_setpoint_c = g_preview_model.to_celsius_from_user(user_val + step);
  if (g_setpoint_c > 35.0f) g_setpoint_c = 35.0f;
}

void on_setpoint_down(lv_event_t *) {
  const float step = (g_preview_unit == thermostat::TemperatureUnit::Fahrenheit) ? 1.0f : 0.5f;
  const float user_val = g_preview_model.to_user_temperature(g_setpoint_c);
  g_setpoint_c = g_preview_model.to_celsius_from_user(user_val - step);
  if (g_setpoint_c < 5.0f) g_setpoint_c = 5.0f;
}

void on_unit_changed(lv_event_t *e) {
  if (e == nullptr) return;
  const auto unit =
      static_cast<thermostat::TemperatureUnit>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_preview_unit = unit;
  thermostat::ui::set_temperature_unit_button_state(unit);
}

uint32_t snap_to_step(uint32_t value, uint32_t step, uint32_t min_v, uint32_t max_v) {
  if (value < min_v) value = min_v;
  if (value > max_v) value = max_v;
  const uint32_t snapped = ((value + (step / 2U)) / step) * step;
  if (snapped < min_v) return min_v;
  if (snapped > max_v) return max_v;
  return snapped;
}

void on_timeout_slider(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  g_display_timeout_s =
      snap_to_step(static_cast<uint32_t>(lv_slider_get_value(slider)), 30U, 30U, 600U);
  lv_slider_set_value(slider, static_cast<int32_t>(g_display_timeout_s), LV_ANIM_OFF);
}

void on_brightness_slider(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  g_brightness_pct = static_cast<uint8_t>(
      snap_to_step(static_cast<uint32_t>(lv_slider_get_value(slider)), 5U, 0U, 100U));
  lv_slider_set_value(slider, static_cast<int32_t>(g_brightness_pct), LV_ANIM_OFF);
}

void on_dim_slider(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  g_screensaver_brightness_pct = static_cast<uint8_t>(
      snap_to_step(static_cast<uint32_t>(lv_slider_get_value(slider)), 5U, 0U, 100U));
  lv_slider_set_value(slider, static_cast<int32_t>(g_screensaver_brightness_pct), LV_ANIM_OFF);
}

void create_ui() {
  thermostat::ui::UiCallbacks callbacks{};
  callbacks.on_tab_changed = on_tab_changed;
  callbacks.on_setpoint_up = on_setpoint_up;
  callbacks.on_setpoint_down = on_setpoint_down;
  callbacks.on_unit = on_unit_changed;
  callbacks.on_timeout_slider = on_timeout_slider;
  callbacks.on_brightness_slider = on_brightness_slider;
  callbacks.on_dim_slider = on_dim_slider;

  thermostat::ui::UiHandles handles{};
  thermostat::ui::build_thermostat_ui(callbacks, &handles);
  g_tabs = handles.tabs;
  g_home_page = handles.home_page;
  g_fan_page = handles.fan_page;
  g_mode_page = handles.mode_page;
  g_settings_page = handles.settings_page;
  g_screensaver_page = handles.screensaver_page;
  g_home_date_label = handles.home_date_label;
  g_home_time_label = handles.home_time_label;
  g_status_label = handles.status_label;
  g_indoor_label = handles.indoor_label;
  g_humidity_label = handles.humidity_label;
  g_setpoint_label = handles.setpoint_label;
  g_weather_label = handles.weather_label;
  g_weather_icon_label = handles.weather_icon_label;
  g_screen_time_label = handles.screen_time_label;
  g_settings_diag_label = handles.settings_diag_label;
  g_settings_display_label = handles.settings_display_label;
  g_timeout_slider = handles.timeout_slider;
  g_brightness_slider = handles.brightness_slider;
  g_dim_slider = handles.dim_slider;

  thermostat::ui::set_temperature_unit_button_state(g_preview_unit);
  show_page(PreviewPage::Home);
  update_labels();
}

bool init_sdl() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  g_window = SDL_CreateWindow("Thermostat UI Preview", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, kDisplayWidth, kDisplayHeight, 0);
  if (g_window == nullptr) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }
  g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
  if (g_renderer == nullptr) {
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (g_renderer == nullptr) {
      fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
      return false;
    }
  }
  g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, kDisplayWidth, kDisplayHeight);
  if (g_texture == nullptr) {
    fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    return false;
  }
  g_framebuffer.resize(static_cast<size_t>(kDisplayWidth * kDisplayHeight), 0xFF000000U);
  return true;
}

void init_lvgl() {
  lv_init();
  g_lv_buf1.resize(static_cast<size_t>(kDisplayWidth * 40));
  g_lv_buf2.resize(static_cast<size_t>(kDisplayWidth * 40));

  lv_disp_draw_buf_init(&g_draw_buf, g_lv_buf1.data(), g_lv_buf2.data(), g_lv_buf1.size());
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kDisplayWidth;
  g_disp_drv.ver_res = kDisplayHeight;
  g_disp_drv.flush_cb = flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = mouse_read_cb;
  lv_indev_drv_register(&g_indev_drv);
}

void tick_lvgl() {
  const uint32_t now = SDL_GetTicks();
  const uint32_t elapsed = now - g_last_tick_ms;
  g_last_tick_ms = now;
  lv_tick_inc(elapsed);
  lv_timer_handler();
}

void process_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      g_running = false;
      return;
    }
    if (e.type == SDL_MOUSEMOTION) {
      g_mouse.x = e.motion.x;
      g_mouse.y = e.motion.y;
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
      g_mouse.pressed = true;
      g_mouse.x = e.button.x;
      g_mouse.y = e.button.y;
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
      g_mouse.pressed = false;
      g_mouse.x = e.button.x;
      g_mouse.y = e.button.y;
    }
  }
}

void render() {
  update_labels();
  SDL_UpdateTexture(g_texture, nullptr, g_framebuffer.data(),
                    kDisplayWidth * static_cast<int>(sizeof(uint32_t)));
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
  SDL_RenderPresent(g_renderer);
}

bool save_bmp_24(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (f == nullptr) {
    fprintf(stderr, "Failed to open %s for write\n", path.c_str());
    return false;
  }

  const int width = kDisplayWidth;
  const int height = kDisplayHeight;
  const int row_stride = ((width * 3) + 3) & ~3;
  const uint32_t pixel_bytes = static_cast<uint32_t>(row_stride * height);
  const uint32_t file_size = 54U + pixel_bytes;

  uint8_t header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  header[2] = static_cast<uint8_t>(file_size & 0xFF);
  header[3] = static_cast<uint8_t>((file_size >> 8) & 0xFF);
  header[4] = static_cast<uint8_t>((file_size >> 16) & 0xFF);
  header[5] = static_cast<uint8_t>((file_size >> 24) & 0xFF);
  header[10] = 54;
  header[14] = 40;
  header[18] = static_cast<uint8_t>(width & 0xFF);
  header[19] = static_cast<uint8_t>((width >> 8) & 0xFF);
  header[20] = static_cast<uint8_t>((width >> 16) & 0xFF);
  header[21] = static_cast<uint8_t>((width >> 24) & 0xFF);
  header[22] = static_cast<uint8_t>(height & 0xFF);
  header[23] = static_cast<uint8_t>((height >> 8) & 0xFF);
  header[24] = static_cast<uint8_t>((height >> 16) & 0xFF);
  header[25] = static_cast<uint8_t>((height >> 24) & 0xFF);
  header[26] = 1;
  header[28] = 24;
  header[34] = static_cast<uint8_t>(pixel_bytes & 0xFF);
  header[35] = static_cast<uint8_t>((pixel_bytes >> 8) & 0xFF);
  header[36] = static_cast<uint8_t>((pixel_bytes >> 16) & 0xFF);
  header[37] = static_cast<uint8_t>((pixel_bytes >> 24) & 0xFF);

  if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
    fclose(f);
    return false;
  }

  std::vector<uint8_t> row(static_cast<size_t>(row_stride), 0);
  for (int y = height - 1; y >= 0; --y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t px = g_framebuffer[static_cast<size_t>(y * width + x)];
      const size_t off = static_cast<size_t>(x * 3);
      row[off + 0] = static_cast<uint8_t>(px & 0xFF);         // B
      row[off + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);  // G
      row[off + 2] = static_cast<uint8_t>((px >> 16) & 0xFF); // R
    }
    if (fwrite(row.data(), 1, static_cast<size_t>(row_stride), f) !=
        static_cast<size_t>(row_stride)) {
      fclose(f);
      return false;
    }
  }

  fclose(f);
  return true;
}

void run_for_ticks(uint32_t ms) {
  const uint32_t start = SDL_GetTicks();
  while (SDL_GetTicks() - start < ms) {
    process_events();
    tick_lvgl();
    render();
    SDL_Delay(5);
  }
}

bool capture_page(PreviewPage page, const char *name) {
  show_page(page);
  run_for_ticks(120);
  const std::string out = g_capture_dir + "/" + std::string(name) + ".bmp";
  return save_bmp_24(out);
}

bool capture_baselines() {
  const std::string mkdir_cmd = "mkdir -p \"" + g_capture_dir + "\"";
  if (system(mkdir_cmd.c_str()) != 0) {
    fprintf(stderr, "Failed to create capture directory %s\n", g_capture_dir.c_str());
    return false;
  }

  bool ok = true;
  ok = ok && capture_page(PreviewPage::Home, "home");
  ok = ok && capture_page(PreviewPage::Fan, "fan");
  ok = ok && capture_page(PreviewPage::Mode, "mode");
  ok = ok && capture_page(PreviewPage::Settings, "settings");
  return ok;
}

void shutdown() {
  if (g_texture != nullptr) {
    SDL_DestroyTexture(g_texture);
    g_texture = nullptr;
  }
  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  SDL_Quit();
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--capture-dir") == 0 && (i + 1) < argc) {
      g_capture_dir = argv[++i];
      g_capture_mode = true;
    }
  }

  if (!init_sdl()) {
    shutdown();
    return 1;
  }
  init_lvgl();
  create_ui();
  g_last_tick_ms = SDL_GetTicks();

  if (g_capture_mode) {
    const bool ok = capture_baselines();
    shutdown();
    return ok ? 0 : 1;
  }

  while (g_running) {
    process_events();
    tick_lvgl();
    render();
    SDL_Delay(5);
  }

  shutdown();
  return 0;
}
