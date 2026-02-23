#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include <time.h>

#include "espnow_cmd_word.h"
#include "sim_mqtt_client.h"
#include "thermostat/display_model.h"
#include "thermostat/thermostat_app.h"
#include "thermostat/thermostat_display_app.h"
#include "thermostat/thermostat_screen_controller.h"
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

// MQTT configuration
constexpr const char *kDisplayBaseTopic = "thermostat/furnace-display";
constexpr const char *kControllerBaseTopic = "thermostat/furnace-controller";
constexpr const char *kMqttClientId = "sim-thermostat";
constexpr const char *kMqttHost = "localhost";
constexpr int kMqttPort = 1883;

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
lv_obj_t *g_screen_weather_label = nullptr;
lv_obj_t *g_screen_indoor_label = nullptr;
lv_obj_t *g_settings_diag_label = nullptr;
lv_obj_t *g_settings_display_label = nullptr;
lv_obj_t *g_settings_system_label = nullptr;
lv_obj_t *g_settings_wifi_label = nullptr;
lv_obj_t *g_settings_mqtt_label = nullptr;
lv_obj_t *g_settings_controller_label = nullptr;
lv_obj_t *g_settings_espnow_label = nullptr;
lv_obj_t *g_settings_config_label = nullptr;
lv_obj_t *g_settings_errors_label = nullptr;
lv_obj_t *g_timeout_slider = nullptr;
lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_dim_slider = nullptr;

thermostat::ThermostatScreenController g_screen;
uint32_t g_display_timeout_s = 300;
uint8_t g_brightness_pct = 100;
uint8_t g_screensaver_brightness_pct = 16;

// MQTT state
sim::SimMqttClient g_mqtt;
bool g_mqtt_connected = false;
uint32_t g_last_mqtt_connect_attempt_ms = 0;
bool g_mqtt_first_attempt = true;

// Heartbeat interval (matches EspNowThermostatTransport default)
constexpr uint32_t kHeartbeatIntervalMs = 10000;
uint32_t g_last_heartbeat_ms = 0;

// Simulated sensor/weather values
float g_preview_indoor_c = 22.2f;
float g_preview_humidity = 43.0f;
float g_preview_outdoor_c = 9.0f;
std::string g_preview_weather_condition = "Cloudy";

// Weather cycle for interactive control
constexpr const char *kWeatherConditions[] = {
    "Clear", "Cloudy", "Rain", "Snow", "Fog", "Lightning"};
constexpr int kWeatherConditionCount = 6;
int g_weather_index = 1;  // Start at Cloudy

std::string display_topic(const char *suffix) {
  return std::string(kDisplayBaseTopic) + "/" + suffix;
}

std::string controller_topic(const char *suffix) {
  return std::string(kControllerBaseTopic) + "/" + suffix;
}

// SimThermostatTransport: bridges ThermostatApp commands to MQTT topics
class SimThermostatTransport : public thermostat::IThermostatTransport {
 public:
  void publish_command_word(uint32_t packed_word) override {
    if (!g_mqtt_connected) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(packed_word));
    g_mqtt.publish(display_topic("state/packed_command"), buf, true);

    printf("[MQTT TX] packed_command=%lu\n", static_cast<unsigned long>(packed_word));
  }

  void publish_controller_ack(uint16_t seq) override {
    if (!g_mqtt_connected) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", seq);
    g_mqtt.publish(display_topic("state/controller_ack"), buf, true);
  }

  void publish_indoor_temperature_c(float temp_c) override {
    if (!g_mqtt_connected) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", temp_c);
    g_mqtt.publish(display_topic("state/current_temp_c"), buf, true);
  }

  void publish_indoor_humidity(float humidity_pct) override {
    if (!g_mqtt_connected) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", humidity_pct);
    g_mqtt.publish(display_topic("state/humidity"), buf, true);
  }
};

SimThermostatTransport g_transport;
thermostat::ThermostatApp *g_therm_app = nullptr;
thermostat::ThermostatDisplayApp *g_display_app = nullptr;
constexpr uint32_t kControllerConnectionTimeoutMs = 30000;

void on_mqtt_message(const std::string &topic, const std::string &payload) {
  const uint32_t now = SDL_GetTicks();

  // Accumulate controller state from individual MQTT topics.
  // The telemetry_seq topic carries the sequence number that ThermostatApp
  // uses for dedup — only feed telemetry to the app when seq changes.
  static FurnaceStateCode s_state = FurnaceStateCode::Error;
  static bool s_lockout = false;
  static uint8_t s_mode_code = 0;
  static uint8_t s_fan_code = 0;
  static float s_setpoint_c = 20.0f;
  static uint32_t s_filter_seconds = 0;
  static uint16_t s_seq = 0;
  static bool s_seq_received = false;

  bool state_updated = false;

  if (topic == controller_topic("state/furnace_state")) {
    s_state = static_cast<FurnaceStateCode>(atoi(payload.c_str()));
    state_updated = true;
  } else if (topic == controller_topic("state/lockout")) {
    s_lockout = (payload == "1");
    state_updated = true;
  } else if (topic == controller_topic("state/mode")) {
    if (payload == "heat") s_mode_code = 1;
    else if (payload == "cool") s_mode_code = 2;
    else s_mode_code = 0;
    state_updated = true;
  } else if (topic == controller_topic("state/fan_mode")) {
    if (payload == "on") s_fan_code = 1;
    else if (payload == "circulate") s_fan_code = 2;
    else s_fan_code = 0;
    state_updated = true;
  } else if (topic == controller_topic("state/target_temp_c")) {
    s_setpoint_c = static_cast<float>(atof(payload.c_str()));
    state_updated = true;
  } else if (topic == controller_topic("state/filter_runtime_hours")) {
    s_filter_seconds = static_cast<uint32_t>(atof(payload.c_str()) * 3600.0f);
    state_updated = true;
  } else if (topic == controller_topic("state/telemetry_seq")) {
    s_seq = static_cast<uint16_t>(atoi(payload.c_str()));
    s_seq_received = true;
    state_updated = true;
  }

  // Weather from controller (simulates ESP-NOW WeatherData packet)
  static float s_ctrl_outdoor_c = 0.0f;
  static std::string s_ctrl_condition;
  static bool s_ctrl_weather_received = false;

  if (topic == controller_topic("state/outdoor_temp_c")) {
    s_ctrl_outdoor_c = static_cast<float>(atof(payload.c_str()));
    if (s_ctrl_weather_received) {
      g_preview_outdoor_c = s_ctrl_outdoor_c;
      g_preview_weather_condition = s_ctrl_condition;
      g_display_app->on_outdoor_weather_update(s_ctrl_outdoor_c, s_ctrl_condition);
    }
    s_ctrl_weather_received = true;
    return;
  }
  if (topic == controller_topic("state/outdoor_condition")) {
    s_ctrl_condition = payload;
    if (s_ctrl_weather_received) {
      g_preview_weather_condition = s_ctrl_condition;
      g_display_app->on_outdoor_weather_update(s_ctrl_outdoor_c, s_ctrl_condition);
      printf("[MQTT RX] Weather from controller: %.1f C, %s\n",
             static_cast<double>(s_ctrl_outdoor_c), s_ctrl_condition.c_str());
    }
    s_ctrl_weather_received = true;
    return;
  }

  if (!state_updated) return;

  // Always note heartbeat when we receive any controller state
  g_therm_app->on_controller_heartbeat(now);

  // Only feed full telemetry once we have a valid sequence number,
  // so ThermostatApp's dedup logic works correctly.
  if (s_seq_received) {
    thermostat::ThermostatControllerTelemetry telemetry;
    telemetry.seq = s_seq;
    telemetry.state = s_state;
    telemetry.lockout = s_lockout;
    telemetry.mode_code = s_mode_code;
    telemetry.fan_code = s_fan_code;
    telemetry.setpoint_c = s_setpoint_c;
    telemetry.filter_runtime_seconds = s_filter_seconds;

    g_therm_app->on_controller_telemetry(now, telemetry);
    g_display_app->sync_from_app();
  }
}

void ensure_mqtt_connected() {
  if (g_mqtt_connected) return;

  const uint32_t now = SDL_GetTicks();
  if (!g_mqtt_first_attempt && (now - g_last_mqtt_connect_attempt_ms < 5000)) return;
  g_mqtt_first_attempt = false;
  g_last_mqtt_connect_attempt_ms = now;

  printf("[MQTT] Connecting to %s:%d...\n", kMqttHost, kMqttPort);
  if (!g_mqtt.connect(kMqttHost, kMqttPort, kMqttClientId)) {
    printf("[MQTT] Connect failed: %s\n", g_mqtt.last_error().c_str());
    return;
  }

  g_mqtt_connected = true;
  printf("[MQTT] Connected!\n");

  // Subscribe to controller state
  g_mqtt.subscribe(controller_topic("state/+"));

  // Publish initial state
  g_mqtt.publish(display_topic("state/availability"), "online", true);

  // Reset command sequence on new connection and request sync
  g_therm_app->reset_local_command_sequence();
  g_therm_app->request_sync(SDL_GetTicks());
}

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

void show_page(thermostat::ThermostatPage page) {
  lv_obj_add_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);

  switch (page) {
    case thermostat::ThermostatPage::Home:
      lv_obj_clear_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case thermostat::ThermostatPage::Fan:
      lv_obj_clear_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case thermostat::ThermostatPage::Mode:
      lv_obj_clear_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case thermostat::ThermostatPage::Settings:
      lv_obj_clear_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case thermostat::ThermostatPage::Screensaver:
      lv_obj_clear_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);
      break;
  }

  if (g_tabs != nullptr) {
    if (page == thermostat::ThermostatPage::Screensaver) {
      lv_obj_add_flag(g_tabs, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(g_tabs, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void activate_screensaver() {
  const uint32_t now = SDL_GetTicks();
  const uint32_t timeout_ms = g_display_timeout_s * 1000U;
  g_screen.on_user_interaction(now - timeout_ms - 1U);
  g_screen.tick(now);
}

void wake_screensaver() { g_screen.on_user_interaction(SDL_GetTicks()); }

void update_labels() {
  const uint32_t now = SDL_GetTicks();

  // Use ThermostatDisplayApp for all formatted text
  const std::string setpoint_text = g_display_app->setpoint_text();
  const std::string indoor_text = g_display_app->indoor_temp_text();
  const std::string humidity_text = g_display_app->indoor_humidity_text();
  const std::string weather_text = g_display_app->weather_text();
  const std::string status_text =
      g_display_app->status_text(now, kControllerConnectionTimeoutMs);

  // Weather icon from display app
  const char *weather_icon = LV_SYMBOL_IMAGE;

  // Live clock
  time_t raw_time;
  time(&raw_time);
  struct tm *tm_info = localtime(&raw_time);
  char date_buf[32];
  char time_buf[16];
  strftime(date_buf, sizeof(date_buf), "%A, %b %d", tm_info);
  strftime(time_buf, sizeof(time_buf), "%-I:%M %p", tm_info);

  if (g_home_date_label != nullptr) {
    lv_label_set_text(g_home_date_label, date_buf);
  }
  if (g_home_time_label != nullptr) {
    lv_label_set_text(g_home_time_label, time_buf);
  }
  lv_label_set_text(g_setpoint_label, setpoint_text.c_str());
  lv_label_set_text(g_status_label, status_text.c_str());
  lv_label_set_text(g_indoor_label, indoor_text.c_str());
  lv_label_set_text(g_humidity_label, humidity_text.c_str());
  lv_label_set_text(g_weather_label, weather_text.c_str());
  if (g_weather_icon_label != nullptr) {
    lv_label_set_text(g_weather_icon_label, weather_icon);
  }
  lv_label_set_text(g_screen_time_label, time_buf);
  if (g_screen_weather_label != nullptr) {
    lv_label_set_text(g_screen_weather_label, weather_text.c_str());
  }
  if (g_screen_indoor_label != nullptr) {
    lv_label_set_text(g_screen_indoor_label, indoor_text.c_str());
  }
  if (g_screen.screensaver_active()) {
    thermostat::ui::update_screensaver_layout(g_screen_time_label, g_screen_weather_label,
                                              g_screen_indoor_label, SDL_GetTicks() / 60000U);
  }

  char system_text[256];
  {
#ifdef THERMOSTAT_FIRMWARE_VERSION
    const char *fw_version = THERMOSTAT_FIRMWARE_VERSION;
#else
    const char *fw_version = "dev";
#endif
    snprintf(system_text, sizeof(system_text),
             "fw: %s\n"
             "build: native-ui-preview\n"
             "boot_count: 1\n"
             "reset: sim\n"
             "uptime_s: %lu",
             fw_version,
             static_cast<unsigned long>(now / 1000U));
  }

  char wifi_text[256];
  snprintf(wifi_text, sizeof(wifi_text),
           "connected: n/a (sim)\n"
           "ip: n/a\n"
           "ssid: n/a\n"
           "mac: n/a\n"
           "channel: n/a\n"
           "rssi_dbm: n/a");

  char mqtt_text[256];
  {
    uint32_t cmd_ago_s = g_last_heartbeat_ms == 0 ? 0 : (now - g_last_heartbeat_ms) / 1000;
    snprintf(mqtt_text, sizeof(mqtt_text),
             "connected: %s\n"
             "state: 0\n"
             "host: %s:%d\n"
             "user: (none)\n"
             "password: unset\n"
             "client_id: %s\n"
             "base_topic: %s\n"
             "last_cmd_ago_s: %lu",
             g_mqtt_connected ? "yes" : "no",
             kMqttHost, kMqttPort,
             kMqttClientId,
             kDisplayBaseTopic,
             static_cast<unsigned long>(cmd_ago_s));
  }

  char controller_text[96];
  {
    uint32_t hb_ms = g_therm_app->last_controller_heartbeat_ms();
    uint32_t hb_ago_s = (hb_ms == 0) ? 0 : (now - hb_ms) / 1000;
    bool connected = g_therm_app->controller_connected(now, kControllerConnectionTimeoutMs);
    snprintf(controller_text, sizeof(controller_text),
             "connected: %s\n"
             "last_hb_ago_s: %lu\n"
             "timeout_ms: %lu",
             connected ? "yes" : "no",
             static_cast<unsigned long>(hb_ago_s),
             static_cast<unsigned long>(kControllerConnectionTimeoutMs));
  }

  char espnow_text[160];
  snprintf(espnow_text, sizeof(espnow_text),
           "channel: n/a (sim)\n"
           "peer_mac: n/a\n"
           "tx_ok: n/a\n"
           "tx_fail: n/a");

  char config_text[192];
  snprintf(config_text, sizeof(config_text),
           "temp_unit: %s\n"
           "temp_comp_c: %.1f\n"
           "display_timeout_s: %lu\n"
           "brightness_pct: %u\n"
           "saver_pct: %u",
           g_display_app->temperature_unit() == thermostat::TemperatureUnit::Fahrenheit ? "F" : "C",
           static_cast<double>(g_display_app->local_temperature_compensation_c()),
           static_cast<unsigned long>(g_display_timeout_s),
           static_cast<unsigned>(g_brightness_pct),
           static_cast<unsigned>(g_screensaver_brightness_pct));

  const char *errors_text =
      "mqtt: none\n"
      "ota: none\n"
      "espnow: none";

  if (g_settings_system_label != nullptr) lv_label_set_text(g_settings_system_label, system_text);
  if (g_settings_wifi_label != nullptr) lv_label_set_text(g_settings_wifi_label, wifi_text);
  if (g_settings_mqtt_label != nullptr) lv_label_set_text(g_settings_mqtt_label, mqtt_text);
  if (g_settings_controller_label != nullptr) lv_label_set_text(g_settings_controller_label, controller_text);
  if (g_settings_espnow_label != nullptr) lv_label_set_text(g_settings_espnow_label, espnow_text);
  if (g_settings_config_label != nullptr) lv_label_set_text(g_settings_config_label, config_text);
  if (g_settings_errors_label != nullptr) lv_label_set_text(g_settings_errors_label, errors_text);
  if (g_settings_display_label != nullptr) lv_label_set_text(g_settings_display_label, system_text);
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

  if (g_settings_diag_label != nullptr) lv_label_set_text(g_settings_diag_label, errors_text);
}

void on_tab_changed(lv_event_t *e) {
  lv_obj_t *btnm = lv_event_get_target(e);
  const char *txt = lv_btnmatrix_get_btn_text(btnm, lv_btnmatrix_get_selected_btn(btnm));
  if (txt == nullptr) {
    return;
  }
  const uint32_t now = SDL_GetTicks();
  if (strcmp(txt, "HOME") == 0) {
    g_screen.on_tab_selected(thermostat::ThermostatPage::Home, now);
  } else if (strcmp(txt, "FAN") == 0) {
    g_screen.on_tab_selected(thermostat::ThermostatPage::Fan, now);
  } else if (strcmp(txt, "MODE") == 0) {
    g_screen.on_tab_selected(thermostat::ThermostatPage::Mode, now);
  } else if (strcmp(txt, "SETTINGS") == 0) {
    g_screen.on_tab_selected(thermostat::ThermostatPage::Settings, now);
  }
  show_page(g_screen.current_page());
}

void on_setpoint_up(lv_event_t *) {
  const uint32_t now = SDL_GetTicks();
  const bool is_f = g_display_app->temperature_unit() == thermostat::TemperatureUnit::Fahrenheit;
  const float step = is_f ? 1.0f : 0.5f;
  // Read current setpoint in user units, step, write back via display app
  const float current_c = g_display_app->local_setpoint_c();
  // Step in user units using the display model's conversion
  thermostat::DisplayModel tmp;
  tmp.set_temperature_unit(g_display_app->temperature_unit());
  const float user_val = tmp.to_user_temperature(current_c);
  const float new_c = tmp.to_celsius_from_user(user_val + step);
  g_display_app->on_user_set_setpoint_c(new_c > 35.0f ? 35.0f : new_c, now);
}

void on_setpoint_down(lv_event_t *) {
  const uint32_t now = SDL_GetTicks();
  const bool is_f = g_display_app->temperature_unit() == thermostat::TemperatureUnit::Fahrenheit;
  const float step = is_f ? 1.0f : 0.5f;
  const float current_c = g_display_app->local_setpoint_c();
  thermostat::DisplayModel tmp;
  tmp.set_temperature_unit(g_display_app->temperature_unit());
  const float user_val = tmp.to_user_temperature(current_c);
  const float new_c = tmp.to_celsius_from_user(user_val - step);
  g_display_app->on_user_set_setpoint_c(new_c < 5.0f ? 5.0f : new_c, now);
}

void on_unit_changed(lv_event_t *e) {
  if (e == nullptr) return;
  const auto unit =
      static_cast<thermostat::TemperatureUnit>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_display_app->set_temperature_unit(unit);
  thermostat::ui::set_temperature_unit_button_state(unit);
}

void on_mode_changed(lv_event_t *e) {
  if (e == nullptr) return;
  const uint32_t now = SDL_GetTicks();
  const auto mode = static_cast<FurnaceMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_display_app->on_user_set_mode(mode, now);
  thermostat::ui::set_mode_button_state(mode);
  g_screen.on_mode_changed(mode);
}

void on_fan_changed(lv_event_t *e) {
  if (e == nullptr) return;
  const uint32_t now = SDL_GetTicks();
  const auto fan = static_cast<FanMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_display_app->on_user_set_fan_mode(fan, now);
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
  g_screen.set_display_timeout_ms(g_display_timeout_s * 1000U);
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
  callbacks.on_mode = on_mode_changed;
  callbacks.on_fan = on_fan_changed;
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
  g_screen_weather_label = handles.screen_weather_label;
  g_screen_indoor_label = handles.screen_indoor_label;
  g_settings_diag_label = handles.settings_diag_label;
  g_settings_display_label = handles.settings_display_label;
  g_settings_system_label = handles.settings_system_label;
  g_settings_wifi_label = handles.settings_wifi_label;
  g_settings_mqtt_label = handles.settings_mqtt_label;
  g_settings_controller_label = handles.settings_controller_label;
  g_settings_espnow_label = handles.settings_espnow_label;
  g_settings_config_label = handles.settings_config_label;
  g_settings_errors_label = handles.settings_errors_label;
  g_timeout_slider = handles.timeout_slider;
  g_brightness_slider = handles.brightness_slider;
  g_dim_slider = handles.dim_slider;

  thermostat::ui::set_mode_button_state(g_display_app->local_mode());
  thermostat::ui::set_temperature_unit_button_state(g_display_app->temperature_unit());
  const uint32_t now = SDL_GetTicks();
  g_screen.on_boot(now);
  g_screen.on_mode_changed(g_display_app->local_mode());
  g_screen.set_display_timeout_ms(g_display_timeout_s * 1000U);
  show_page(g_screen.current_page());
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
      g_screen.on_user_interaction(SDL_GetTicks());
    } else if (e.type == SDL_KEYDOWN) {
      if (e.key.keysym.sym == SDLK_s) {
        activate_screensaver();
      } else if (e.key.keysym.sym == SDLK_w) {
        // W key: cycle weather condition
        g_weather_index = (g_weather_index + 1) % kWeatherConditionCount;
        g_preview_weather_condition = kWeatherConditions[g_weather_index];
        g_display_app->on_outdoor_weather_update(g_preview_outdoor_c,
                                                  g_preview_weather_condition);
        printf("[SIM] Weather: %s, Outdoor: %.1f C\n",
               g_preview_weather_condition.c_str(), g_preview_outdoor_c);
      } else if (e.key.keysym.sym == SDLK_RIGHTBRACKET) {
        // ] key: increase outdoor temperature
        g_preview_outdoor_c += 2.0f;
        if (g_preview_outdoor_c > 50.0f) g_preview_outdoor_c = 50.0f;
        g_display_app->on_outdoor_weather_update(g_preview_outdoor_c,
                                                  g_preview_weather_condition);
        printf("[SIM] Outdoor temp: %.1f C\n", g_preview_outdoor_c);
      } else if (e.key.keysym.sym == SDLK_LEFTBRACKET) {
        // [ key: decrease outdoor temperature
        g_preview_outdoor_c -= 2.0f;
        if (g_preview_outdoor_c < -40.0f) g_preview_outdoor_c = -40.0f;
        g_display_app->on_outdoor_weather_update(g_preview_outdoor_c,
                                                  g_preview_weather_condition);
        printf("[SIM] Outdoor temp: %.1f C\n", g_preview_outdoor_c);
      } else if (e.key.keysym.sym == SDLK_d) {
        // D key: wake display (renamed from W since W is now weather)
        wake_screensaver();
      }
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
    g_screen.tick(SDL_GetTicks());
    show_page(g_screen.current_page());
    tick_lvgl();
    render();
    SDL_Delay(5);
  }
}

bool capture_page(thermostat::ThermostatPage page, const char *name) {
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
  ok = ok && capture_page(thermostat::ThermostatPage::Home, "home");
  ok = ok && capture_page(thermostat::ThermostatPage::Fan, "fan");
  ok = ok && capture_page(thermostat::ThermostatPage::Mode, "mode");
  ok = ok && capture_page(thermostat::ThermostatPage::Settings, "settings");
  activate_screensaver();
  ok = ok && capture_page(thermostat::ThermostatPage::Screensaver, "screensaver");
  return ok;
}

void shutdown() {
  if (g_mqtt_connected) {
    g_mqtt.publish(display_topic("state/availability"), "offline", true);
    g_mqtt.disconnect();
  }

  delete g_display_app;
  g_display_app = nullptr;
  delete g_therm_app;
  g_therm_app = nullptr;

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

  // Initialize app layer
  g_therm_app = new thermostat::ThermostatApp(g_transport);
  g_display_app = new thermostat::ThermostatDisplayApp(*g_therm_app);
  g_display_app->set_temperature_unit(thermostat::TemperatureUnit::Celsius);
  g_display_app->set_local_temperature_compensation_c(-0.3f);

  // Seed initial sensor/weather data
  g_display_app->on_local_sensor_update(g_preview_indoor_c, g_preview_humidity);
  g_display_app->on_outdoor_weather_update(g_preview_outdoor_c, g_preview_weather_condition);

  init_lvgl();
  create_ui();
  g_last_tick_ms = SDL_GetTicks();

  if (g_capture_mode) {
    const bool ok = capture_baselines();
    shutdown();
    return ok ? 0 : 1;
  }

  fprintf(stdout, "Simulator controls:\n");
  fprintf(stdout, "  S = activate screensaver, D = wake display\n");
  fprintf(stdout, "  W = cycle weather, [ / ] = adjust outdoor temp\n");
  fprintf(stdout, "Connecting to MQTT broker at %s:%d\n", kMqttHost, kMqttPort);

  // Setup MQTT callback
  g_mqtt.set_message_callback(on_mqtt_message);

  while (g_running) {
    ensure_mqtt_connected();
    g_mqtt.loop();
    process_events();

    // Periodic heartbeat: re-publish last command so the controller
    // doesn't trip its failsafe timer (mirrors ESP-NOW heartbeat).
    const uint32_t now = SDL_GetTicks();
    if (g_mqtt_connected && g_therm_app->has_last_packed_command() &&
        (now - g_last_heartbeat_ms) >= kHeartbeatIntervalMs) {
      g_last_heartbeat_ms = now;
      char buf[32];
      snprintf(buf, sizeof(buf), "%lu",
               static_cast<unsigned long>(g_therm_app->last_packed_command()));
      g_mqtt.publish(display_topic("state/packed_command"), buf, true);
    }

    g_screen.tick(now);
    show_page(g_screen.current_page());
    tick_lvgl();
    render();
    SDL_Delay(5);
  }

  shutdown();
  return 0;
}
