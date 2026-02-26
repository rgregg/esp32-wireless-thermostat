#if defined(ARDUINO) && defined(THERMOSTAT_ROLE_THERMOSTAT)

#include "thermostat/esp32s3_thermostat_firmware.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdio>

#include <Wire.h>
#include <lvgl.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include "improv_ble_provisioning.h"
#include <PubSubClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include "ota_web_updater.h"

#include "thermostat/thermostat_device_runtime.h"
#include "mqtt_payload.h"
#include "weather_icon.h"
#include "thermostat/thermostat_screen_controller.h"
#include "thermostat/ui/thermostat_ui_shared.h"
#include "management_paths.h"

#include <Adafruit_AHTX0.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace thermostat {
namespace {

constexpr int kDisplayWidth = 800;
constexpr int kDisplayHeight = 480;
constexpr uint32_t kUiTickMs = 5;
constexpr uint32_t kRuntimeTickMs = 200;
constexpr uint32_t kSensorPollMs = 60000;
constexpr uint32_t kUiRefreshMs = 500;

constexpr int kBacklightPin = 2;
constexpr int kBacklightFreq = 800;
constexpr int kBacklightResolution = 8;

constexpr uint8_t kGt911Addr = 0x5D;

constexpr int kTouchI2cSda = 19;
constexpr int kTouchI2cScl = 20;
constexpr int kTouchRstPin = 38;
constexpr int kSensorI2cSda = 18;
constexpr int kSensorI2cScl = 17;
constexpr uint32_t kNetworkRetryMs = 5000;
constexpr uint32_t kProvisionStartDelayMs = 15000;
constexpr uint32_t kMqttPublishMs = 10000;
constexpr uint32_t kWeatherPollMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr uint32_t kDisplayInitSettleMs = 150;
constexpr uint32_t kBacklightEnableDelayMs = 400;
constexpr uint32_t kRebootDelayMs = 1000;
constexpr uint32_t kRebootPanelOffDelayMs = 200;

#ifndef THERMOSTAT_WIFI_SSID
#define THERMOSTAT_WIFI_SSID ""
#endif

#ifndef THERMOSTAT_WIFI_PASSWORD
#define THERMOSTAT_WIFI_PASSWORD ""
#endif

#ifndef THERMOSTAT_MQTT_HOST
#define THERMOSTAT_MQTT_HOST "mqtt.lan"
#endif

#ifndef THERMOSTAT_MQTT_PORT
#define THERMOSTAT_MQTT_PORT 1883
#endif

#ifndef THERMOSTAT_MQTT_USER
#define THERMOSTAT_MQTT_USER ""
#endif

#ifndef THERMOSTAT_MQTT_PASSWORD
#define THERMOSTAT_MQTT_PASSWORD ""
#endif

#ifndef THERMOSTAT_MQTT_CLIENT_ID
#define THERMOSTAT_MQTT_CLIENT_ID "esp32-furnace-thermostat"
#endif

#ifndef THERMOSTAT_MQTT_NODE_ID
#define THERMOSTAT_MQTT_NODE_ID "esp32_wireless_thermostat"
#endif

#ifndef THERMOSTAT_MQTT_BASE_TOPIC
#define THERMOSTAT_MQTT_BASE_TOPIC "thermostat/furnace-display"
#endif

#ifndef THERMOSTAT_MQTT_DISCOVERY_PREFIX
#define THERMOSTAT_MQTT_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef THERMOSTAT_MQTT_SHARED_DEVICE_ID
#define THERMOSTAT_MQTT_SHARED_DEVICE_ID "wireless_thermostat_system"
#endif

#ifndef THERMOSTAT_PIRATEWEATHER_API_KEY
#define THERMOSTAT_PIRATEWEATHER_API_KEY ""
#endif

#ifndef THERMOSTAT_PIRATEWEATHER_ZIP
#define THERMOSTAT_PIRATEWEATHER_ZIP ""
#endif


#ifndef THERMOSTAT_DISPLAY_TIMEOUT_S
#define THERMOSTAT_DISPLAY_TIMEOUT_S 300
#endif

#ifndef THERMOSTAT_BACKLIGHT_ACTIVE_PCT
#define THERMOSTAT_BACKLIGHT_ACTIVE_PCT 100
#endif

#ifndef THERMOSTAT_BACKLIGHT_SCREENSAVER_PCT
#define THERMOSTAT_BACKLIGHT_SCREENSAVER_PCT 16
#endif

#ifndef THERMOSTAT_TIME_TZ
#define THERMOSTAT_TIME_TZ "PST8PDT,M3.2.0,M11.1.0"
#endif

#ifndef THERMOSTAT_TIME_NTP_1
#define THERMOSTAT_TIME_NTP_1 "0.pool.ntp.org"
#endif

#ifndef THERMOSTAT_TIME_NTP_2
#define THERMOSTAT_TIME_NTP_2 "1.pool.ntp.org"
#endif

#ifndef THERMOSTAT_TIME_NTP_3
#define THERMOSTAT_TIME_NTP_3 "2.pool.ntp.org"
#endif

#ifndef THERMOSTAT_ESPNOW_PEER_MAC
#define THERMOSTAT_ESPNOW_PEER_MAC "FF:FF:FF:FF:FF:FF"
#endif

#ifndef THERMOSTAT_ESPNOW_LMK
#define THERMOSTAT_ESPNOW_LMK "a1b2c3d4e5f60718293a4b5c6d7e8f90"
#endif

#ifndef THERMOSTAT_ESPNOW_CHANNEL
#define THERMOSTAT_ESPNOW_CHANNEL 6
#endif

#ifndef THERMOSTAT_FIRMWARE_VERSION
#define THERMOSTAT_FIRMWARE_VERSION "dev"
#endif

#ifndef THERMOSTAT_OTA_HOSTNAME
#define THERMOSTAT_OTA_HOSTNAME "esp32-furnace-thermostat"
#endif

#ifndef THERMOSTAT_OTA_PASSWORD
#define THERMOSTAT_OTA_PASSWORD ""
#endif

struct TouchState {
  bool touched = false;
  int16_t x = 0;
  int16_t y = 0;
};

uint16_t g_touch_x_max = kDisplayWidth;
uint16_t g_touch_y_max = kDisplayHeight;

esp_lcd_panel_handle_t g_panel = nullptr;
SemaphoreHandle_t g_flush_ready_sem = nullptr;

// Called from ISR when the panel finishes swapping framebuffers at VSYNC
bool IRAM_ATTR on_vsync_ready(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                               void *) {
  BaseType_t high_task_wakeup = pdFALSE;
  if (g_flush_ready_sem != nullptr) {
    xSemaphoreGiveFromISR(g_flush_ready_sem, &high_task_wakeup);
  }
  return high_task_wakeup == pdTRUE;
}

lv_disp_draw_buf_t g_draw_buf;
lv_color_t *g_buf_1 = nullptr;
lv_color_t *g_buf_2 = nullptr;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;

TouchState g_touch{};
Adafruit_AHTX0 g_aht;
bool g_aht_ready = false;
TwoWire g_touch_i2c(0);
TwoWire g_sensor_i2c(1);
Preferences g_cfg;
bool g_cfg_ready = false;
WiFiClient g_wifi_client;
PubSubClient g_mqtt(g_wifi_client);
WebServer g_web(80);
bool g_mqtt_discovery_sent = false;
bool g_web_started = false;
bool g_mdns_started = false;

ThermostatDeviceRuntime *g_runtime = nullptr;
ThermostatScreenController g_screen;

lv_obj_t *g_tabs = nullptr;
lv_obj_t *g_home_page = nullptr;
lv_obj_t *g_fan_page = nullptr;
lv_obj_t *g_mode_page = nullptr;
lv_obj_t *g_settings_page = nullptr;
lv_obj_t *g_screensaver_page = nullptr;

lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_indoor_label = nullptr;
lv_obj_t *g_humidity_label = nullptr;
lv_obj_t *g_setpoint_label = nullptr;
lv_obj_t *g_weather_label = nullptr;
lv_obj_t *g_weather_icon_label = nullptr;
lv_obj_t *g_home_time_label = nullptr;
lv_obj_t *g_home_date_label = nullptr;
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

lv_obj_t *g_setpoint_column = nullptr;
lv_obj_t *g_filter_label = nullptr;
lv_obj_t *g_fan_status_label = nullptr;
lv_obj_t *g_mode_status_label = nullptr;
lv_obj_t *g_timeout_slider = nullptr;
lv_obj_t *g_brightness_slider = nullptr;
lv_obj_t *g_dim_slider = nullptr;

uint32_t g_last_ui_tick_ms = 0;
uint32_t g_last_runtime_tick_ms = 0;
uint32_t g_last_sensor_poll_ms = 0;
uint32_t g_last_ui_refresh_ms = 0;
uint32_t g_last_wifi_attempt_ms = 0;
uint32_t g_first_wifi_attempt_ms = 0;
uint32_t g_last_mqtt_attempt_ms = 0;
uint32_t g_last_mqtt_publish_ms = 0;
uint32_t g_last_mqtt_command_ms = 0;
bool g_wifi_has_attempted_stored_connect = false;
bool g_wifi_provisioning_started = false;
float g_outdoor_temp_c = 6.0f;
WeatherIcon g_weather_icon = WeatherIcon::Cloudy;
bool g_have_weather_data = false;
uint32_t g_last_weather_poll_ms = 0;
uint32_t g_display_timeout_ms = static_cast<uint32_t>(THERMOSTAT_DISPLAY_TIMEOUT_S) * 1000UL;
uint8_t g_cfg_backlight_active_pct = THERMOSTAT_BACKLIGHT_ACTIVE_PCT;
uint8_t g_cfg_backlight_screensaver_pct = THERMOSTAT_BACKLIGHT_SCREENSAVER_PCT;
bool g_ota_started = false;
float g_cfg_temp_comp_c = 0.0f;
bool g_cfg_temp_unit_f = false;
String g_cfg_wifi_ssid = THERMOSTAT_WIFI_SSID;
String g_cfg_wifi_password = THERMOSTAT_WIFI_PASSWORD;
String g_cfg_mqtt_host = THERMOSTAT_MQTT_HOST;
uint16_t g_cfg_mqtt_port = THERMOSTAT_MQTT_PORT;
String g_cfg_mqtt_user = THERMOSTAT_MQTT_USER;
String g_cfg_mqtt_password = THERMOSTAT_MQTT_PASSWORD;
String g_cfg_mqtt_client_id = THERMOSTAT_MQTT_CLIENT_ID;
String g_cfg_mqtt_base_topic = THERMOSTAT_MQTT_BASE_TOPIC;
String g_cfg_discovery_prefix = THERMOSTAT_MQTT_DISCOVERY_PREFIX;
String g_cfg_shared_device_id = THERMOSTAT_MQTT_SHARED_DEVICE_ID;
String g_cfg_pirateweather_api_key = THERMOSTAT_PIRATEWEATHER_API_KEY;
String g_cfg_pirateweather_zip = THERMOSTAT_PIRATEWEATHER_ZIP;
String g_cfg_ota_hostname = THERMOSTAT_OTA_HOSTNAME;
String g_cfg_ota_password = THERMOSTAT_OTA_PASSWORD;
uint8_t g_cfg_espnow_channel = THERMOSTAT_ESPNOW_CHANNEL;
String g_cfg_espnow_peer_mac = THERMOSTAT_ESPNOW_PEER_MAC;
String g_cfg_espnow_lmk = THERMOSTAT_ESPNOW_LMK;
String g_cfg_controller_base_topic = "";
uint32_t g_cfg_controller_timeout_ms = 30000;
bool g_cfg_wifi_reconnect_required = false;
bool g_cfg_mqtt_reconfigure_required = false;
bool g_cfg_reboot_required = false;
uint32_t g_boot_count = 0;
String g_reset_reason = "unknown";
String g_last_mqtt_error = "none";
String g_last_ota_error = "none";
String g_last_espnow_error = "none";
bool g_reboot_requested = false;
uint32_t g_reboot_at_ms = 0;
bool g_backlight_enabled = false;
uint32_t g_backlight_enable_at_ms = 0;

// Shadow state for controller telemetry received via MQTT
FurnaceStateCode g_mqtt_ctrl_state = FurnaceStateCode::Error;
bool g_mqtt_ctrl_lockout = false;
FurnaceMode g_mqtt_ctrl_mode = FurnaceMode::Off;
FanMode g_mqtt_ctrl_fan = FanMode::Automatic;
float g_mqtt_ctrl_setpoint_c = 20.0f;
uint32_t g_mqtt_ctrl_filter_runtime_s = 0;
uint32_t g_mqtt_ctrl_last_update_ms = 0;
uint32_t g_mqtt_ctrl_last_applied_ms = 0;
bool g_mqtt_ctrl_available = false;

uint8_t clamp_percent(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}

uint8_t percent_to_duty(uint8_t percent) {
  if (percent >= 100) {
    return 255;
  }
  return static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U + 50U) / 100U);
}

const char *mode_to_mqtt(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Heat:
      return "heat";
    case FurnaceMode::Cool:
      return "cool";
    case FurnaceMode::Off:
    default:
      return "off";
  }
}

const char *fan_to_mqtt(FanMode mode) {
  switch (mode) {
    case FanMode::AlwaysOn:
      return "on";
    case FanMode::Circulate:
      return "circulate";
    case FanMode::Automatic:
    default:
      return "auto";
  }
}

void load_runtime_config() {
  if (!g_cfg_ready) return;
  g_cfg_wifi_ssid = g_cfg.getString("wifi_ssid", g_cfg_wifi_ssid);
  g_cfg_wifi_password = g_cfg.getString("wifi_pwd", g_cfg_wifi_password);
  g_cfg_mqtt_host = g_cfg.getString("mqtt_host", g_cfg_mqtt_host);
  g_cfg_mqtt_port = static_cast<uint16_t>(g_cfg.getUInt("mqtt_port", g_cfg_mqtt_port));
  g_cfg_mqtt_user = g_cfg.getString("mqtt_user", g_cfg_mqtt_user);
  g_cfg_mqtt_password = g_cfg.getString("mqtt_pwd", g_cfg_mqtt_password);
  g_cfg_mqtt_client_id = g_cfg.getString("mqtt_cid", g_cfg_mqtt_client_id);
  g_cfg_mqtt_base_topic = g_cfg.getString("mqtt_base", g_cfg_mqtt_base_topic);
  g_cfg_discovery_prefix = g_cfg.getString("disc_pref", g_cfg_discovery_prefix);
  g_cfg_shared_device_id = g_cfg.getString("shared_id", g_cfg_shared_device_id);
  g_cfg_pirateweather_api_key = g_cfg.getString("pw_key", g_cfg_pirateweather_api_key);
  g_cfg_pirateweather_zip = g_cfg.getString("pw_zip", g_cfg_pirateweather_zip);
  g_cfg_ota_hostname = g_cfg.getString("ota_host", g_cfg_ota_hostname);
  g_cfg_ota_password = g_cfg.getString("ota_pwd", g_cfg_ota_password);
  g_cfg_espnow_channel = static_cast<uint8_t>(g_cfg.getUChar("esp_ch", g_cfg_espnow_channel));
  g_cfg_espnow_peer_mac = g_cfg.getString("esp_peer", g_cfg_espnow_peer_mac);
  g_cfg_espnow_lmk = g_cfg.getString("esp_lmk", g_cfg_espnow_lmk);
  g_cfg_controller_base_topic = g_cfg.getString("ctrl_base", g_cfg_controller_base_topic);
  g_cfg_controller_timeout_ms = g_cfg.getUInt("ctrl_to", g_cfg_controller_timeout_ms);
  const uint32_t display_timeout_s = g_cfg.getUInt("disp_to_s", THERMOSTAT_DISPLAY_TIMEOUT_S);
  g_display_timeout_ms = display_timeout_s * 1000UL;
  g_cfg_backlight_active_pct =
      clamp_percent(g_cfg.getUChar("bl_act", g_cfg_backlight_active_pct));
  g_cfg_backlight_screensaver_pct =
      clamp_percent(g_cfg.getUChar("bl_dim", g_cfg_backlight_screensaver_pct));
  g_cfg_temp_comp_c = g_cfg.getFloat("temp_comp", 0.0f);
  g_cfg_temp_unit_f = g_cfg.getBool("temp_u_f", false);
}

String topic_for(const char *suffix) {
  String out(g_cfg_mqtt_base_topic);
  out += "/";
  out += suffix;
  return out;
}

String controller_topic_for(const char *suffix) {
  String out(g_cfg_controller_base_topic);
  out += "/";
  out += suffix;
  return out;
}

void publish_cfg_value(const char *key, const String &value) {
  if (!g_mqtt.connected()) return;
  String topic = topic_for("cfg");
  topic += "/";
  topic += key;
  topic += "/state";
  const char *payload = value.c_str();
  if (thermostat::management_paths::is_secret_cfg_key(key)) {
    payload = value.length() > 0 ? "set" : "unset";
  }
  g_mqtt.publish(topic.c_str(), payload, true);
}

void publish_all_cfg_state() {
  publish_cfg_value("wifi_ssid", g_cfg_wifi_ssid);
  publish_cfg_value("wifi_password", g_cfg_wifi_password);
  publish_cfg_value("mqtt_host", g_cfg_mqtt_host);
  publish_cfg_value("mqtt_port", String(g_cfg_mqtt_port));
  publish_cfg_value("mqtt_user", g_cfg_mqtt_user);
  publish_cfg_value("mqtt_password", g_cfg_mqtt_password);
  publish_cfg_value("mqtt_client_id", g_cfg_mqtt_client_id);
  publish_cfg_value("mqtt_base_topic", g_cfg_mqtt_base_topic);
  publish_cfg_value("discovery_prefix", g_cfg_discovery_prefix);
  publish_cfg_value("shared_device_id", g_cfg_shared_device_id);
  publish_cfg_value("pirateweather_api_key", g_cfg_pirateweather_api_key);
  publish_cfg_value("pirateweather_zip", g_cfg_pirateweather_zip);
  publish_cfg_value("display_timeout_s", String(g_display_timeout_ms / 1000UL));
  publish_cfg_value("backlight_active_pct", String(g_cfg_backlight_active_pct));
  publish_cfg_value("backlight_screensaver_pct", String(g_cfg_backlight_screensaver_pct));
  publish_cfg_value("temp_comp_c", String(g_cfg_temp_comp_c, 2));
  publish_cfg_value("temperature_unit", g_cfg_temp_unit_f ? "f" : "c");
  publish_cfg_value("ota_hostname", g_cfg_ota_hostname);
  publish_cfg_value("ota_password", g_cfg_ota_password);
  publish_cfg_value("espnow_channel", String(g_cfg_espnow_channel));
  publish_cfg_value("espnow_peer_mac", g_cfg_espnow_peer_mac);
  publish_cfg_value("espnow_lmk", g_cfg_espnow_lmk);
  publish_cfg_value("controller_base_topic", g_cfg_controller_base_topic);
  publish_cfg_value("controller_timeout_ms", String(g_cfg_controller_timeout_ms));
  publish_cfg_value("reboot_required", g_cfg_reboot_required ? "1" : "0");
}

bool try_update_runtime_config(const String &key, const char *raw_value) {
  if (!g_cfg_ready || raw_value == nullptr) return false;
  const String value(raw_value);
  bool known = true;
  if (key == "wifi_ssid") {
    g_cfg_wifi_ssid = value;
    g_cfg.putString("wifi_ssid", value);
    g_cfg_wifi_reconnect_required = true;
  } else if (key == "wifi_password") {
    g_cfg_wifi_password = value;
    g_cfg.putString("wifi_pwd", value);
    g_cfg_wifi_reconnect_required = true;
  } else if (key == "mqtt_host") {
    g_cfg_mqtt_host = value;
    g_cfg.putString("mqtt_host", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_port") {
    const long parsed = atol(raw_value);
    if (parsed < 1 || parsed > 65535) return false;
    g_cfg_mqtt_port = static_cast<uint16_t>(parsed);
    g_cfg.putUInt("mqtt_port", g_cfg_mqtt_port);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_user") {
    g_cfg_mqtt_user = value;
    g_cfg.putString("mqtt_user", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_password") {
    g_cfg_mqtt_password = value;
    g_cfg.putString("mqtt_pwd", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_client_id") {
    g_cfg_mqtt_client_id = value;
    g_cfg.putString("mqtt_cid", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_base_topic") {
    g_cfg_mqtt_base_topic = value;
    g_cfg.putString("mqtt_base", value);
    g_cfg_mqtt_reconfigure_required = true;
    g_mqtt_discovery_sent = false;
  } else if (key == "discovery_prefix") {
    g_cfg_discovery_prefix = value;
    g_cfg.putString("disc_pref", value);
    g_mqtt_discovery_sent = false;
  } else if (key == "shared_device_id") {
    g_cfg_shared_device_id = value;
    g_cfg.putString("shared_id", value);
    g_mqtt_discovery_sent = false;
  } else if (key == "pirateweather_api_key") {
    g_cfg_pirateweather_api_key = value;
    g_cfg.putString("pw_key", value);
    g_have_weather_data = false;
    g_last_weather_poll_ms = 0;
  } else if (key == "pirateweather_zip") {
    g_cfg_pirateweather_zip = value;
    g_cfg.putString("pw_zip", value);
    g_have_weather_data = false;
    g_last_weather_poll_ms = 0;
  } else if (key == "display_timeout_s") {
    long seconds = atol(raw_value);
    if (seconds < 30) seconds = 30;
    if (seconds > 600) seconds = 600;
    g_display_timeout_ms = static_cast<uint32_t>(seconds) * 1000UL;
    g_cfg.putUInt("disp_to_s", static_cast<uint32_t>(seconds));
    g_screen.set_display_timeout_ms(g_display_timeout_ms);
  } else if (key == "backlight_active_pct") {
    g_cfg_backlight_active_pct = clamp_percent(atol(raw_value));
    g_cfg.putUChar("bl_act", g_cfg_backlight_active_pct);
  } else if (key == "backlight_screensaver_pct") {
    g_cfg_backlight_screensaver_pct = clamp_percent(atol(raw_value));
    g_cfg.putUChar("bl_dim", g_cfg_backlight_screensaver_pct);
  } else if (key == "temp_comp_c") {
    g_cfg_temp_comp_c = static_cast<float>(atof(raw_value));
    g_cfg.putFloat("temp_comp", g_cfg_temp_comp_c);
    if (g_runtime != nullptr) {
      g_runtime->set_local_temperature_compensation_c(g_cfg_temp_comp_c);
    }
  } else if (key == "temperature_unit") {
    g_cfg_temp_unit_f = (value == "f" || value == "fahrenheit");
    g_cfg.putBool("temp_u_f", g_cfg_temp_unit_f);
    if (g_runtime != nullptr) {
      g_runtime->set_temperature_unit(g_cfg_temp_unit_f ? TemperatureUnit::Fahrenheit
                                                        : TemperatureUnit::Celsius);
    }
  } else if (key == "ota_hostname") {
    g_cfg_ota_hostname = value;
    g_cfg.putString("ota_host", value);
  } else if (key == "ota_password") {
    g_cfg_ota_password = value;
    g_cfg.putString("ota_pwd", value);
  } else if (key == "espnow_channel") {
    const long parsed = atol(raw_value);
    if (parsed < 1 || parsed > 14) return false;
    g_cfg_espnow_channel = static_cast<uint8_t>(parsed);
    g_cfg.putUChar("esp_ch", g_cfg_espnow_channel);
    g_cfg_reboot_required = true;
  } else if (key == "espnow_peer_mac") {
    g_cfg_espnow_peer_mac = value;
    g_cfg.putString("esp_peer", value);
    g_cfg_reboot_required = true;
  } else if (key == "espnow_lmk") {
    g_cfg_espnow_lmk = value;
    g_cfg.putString("esp_lmk", value);
    g_cfg_reboot_required = true;
  } else if (key == "controller_base_topic") {
    g_cfg_controller_base_topic = value;
    g_cfg.putString("ctrl_base", value);
  } else if (key == "controller_timeout_ms") {
    const long parsed = atol(raw_value);
    if (parsed < 1000 || parsed > 600000) return false;
    g_cfg_controller_timeout_ms = static_cast<uint32_t>(parsed);
    g_cfg.putUInt("ctrl_to", g_cfg_controller_timeout_ms);
    g_cfg_reboot_required = true;
  } else {
    known = false;
  }
  if (!known) return false;
  String state_value(value);
  if (key == "display_timeout_s") {
    state_value = String(g_display_timeout_ms / 1000UL);
  } else if (key == "backlight_active_pct") {
    state_value = String(g_cfg_backlight_active_pct);
  } else if (key == "backlight_screensaver_pct") {
    state_value = String(g_cfg_backlight_screensaver_pct);
  }
  if (g_mqtt.connected()) {
    publish_cfg_value(key.c_str(), state_value);
  }
  return true;
}

bool parse_bool_payload(const char *value) {
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
}

void schedule_reboot() {
  g_reboot_requested = true;
  g_reboot_at_ms = millis() + kRebootDelayMs;
}

void shutdown_display_for_reboot() {
  ledcWrite(kBacklightPin, 0);
  if (g_panel != nullptr) {
    esp_lcd_panel_disp_on_off(g_panel, false);
  }
}

void lower_in_place(char *text) {
  for (size_t i = 0; text[i] != '\0'; ++i) {
    text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
  }
}

float parse_numeric_prefix(const std::string &text, bool *ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  if (text.empty()) {
    return 0.0f;
  }
  if (!((text[0] >= '0' && text[0] <= '9') || text[0] == '-' || text[0] == '+')) {
    return 0.0f;
  }
  char *end = nullptr;
  const float value = strtof(text.c_str(), &end);
  if (end == text.c_str()) {
    return 0.0f;
  }
  if (ok != nullptr) {
    *ok = true;
  }
  return value;
}

String normalize_zip(const String &raw_zip) {
  String out;
  out.reserve(5);
  for (size_t i = 0; i < raw_zip.length(); ++i) {
    const char c = raw_zip[i];
    if (c == '-') {
      break;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      if (out.length() < 5) {
        out += c;
      }
    }
  }
  return out.length() == 5 ? out : String("");
}

bool json_extract_string(const String &json, const char *key, String *out, int from_index = 0) {
  if (out == nullptr || key == nullptr) return false;
  const String token = "\"" + String(key) + "\"";
  const int key_index = json.indexOf(token, from_index);
  if (key_index < 0) return false;
  const int colon_index = json.indexOf(':', key_index + token.length());
  if (colon_index < 0) return false;
  int value_start = colon_index + 1;
  while (value_start < static_cast<int>(json.length()) &&
         std::isspace(static_cast<unsigned char>(json[value_start]))) {
    ++value_start;
  }
  if (value_start >= static_cast<int>(json.length()) || json[value_start] != '"') return false;
  ++value_start;

  String value;
  bool escaped = false;
  for (int i = value_start; i < static_cast<int>(json.length()); ++i) {
    const char c = json[i];
    if (c == '"' && !escaped) {
      *out = value;
      return true;
    }
    if (escaped) {
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
      continue;
    }
    value += c;
  }
  return false;
}

bool json_extract_float(const String &json, const char *key, float *out, int from_index = 0) {
  if (out == nullptr || key == nullptr) return false;
  const String token = "\"" + String(key) + "\"";
  const int key_index = json.indexOf(token, from_index);
  if (key_index < 0) return false;
  const int colon_index = json.indexOf(':', key_index + token.length());
  if (colon_index < 0) return false;
  int value_start = colon_index + 1;
  while (value_start < static_cast<int>(json.length()) &&
         std::isspace(static_cast<unsigned char>(json[value_start]))) {
    ++value_start;
  }
  if (value_start >= static_cast<int>(json.length())) return false;

  bool quoted = json[value_start] == '"';
  if (quoted) ++value_start;
  int value_end = value_start;
  while (value_end < static_cast<int>(json.length())) {
    const char c = json[value_end];
    if (quoted) {
      if (c == '"') break;
    } else if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
      break;
    }
    ++value_end;
  }
  if (value_end <= value_start) return false;
  *out = static_cast<float>(atof(json.substring(value_start, value_end).c_str()));
  return true;
}

WeatherIcon map_pirateweather_icon(const String &icon) {
  return weather_icon_from_api(icon.c_str());
}

bool fetch_zip_coordinates(const String &zip, float *lat_out, float *lon_out) {
  if (lat_out == nullptr || lon_out == nullptr || zip.length() == 0) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const String url = "https://api.zippopotam.us/us/" + zip;
  if (!http.begin(client, url)) return false;
  http.setTimeout(kHttpTimeoutMs);
  const int status = http.GET();
  if (status != 200) {
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  float lat = 0.0f;
  float lon = 0.0f;
  if (!json_extract_float(body, "latitude", &lat) || !json_extract_float(body, "longitude", &lon)) {
    return false;
  }
  *lat_out = lat;
  *lon_out = lon;
  return true;
}

bool fetch_pirateweather_current(float lat, float lon, float *temp_c_out, WeatherIcon *icon_out) {
  if (temp_c_out == nullptr || icon_out == nullptr || g_cfg_pirateweather_api_key.length() == 0) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  char coord[40];
  snprintf(coord, sizeof(coord), "%.5f,%.5f", static_cast<double>(lat), static_cast<double>(lon));
  const String url = "https://api.pirateweather.net/forecast/" + g_cfg_pirateweather_api_key +
                     "/" + String(coord) + "?units=si&exclude=minutely,hourly,daily,alerts";
  if (!http.begin(client, url)) return false;
  http.setTimeout(kHttpTimeoutMs);
  const int status = http.GET();
  if (status != 200) {
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();
  const int current_idx = body.indexOf("\"currently\"");
  if (current_idx < 0) return false;

  float temp_c = 0.0f;
  String icon;
  if (!json_extract_float(body, "temperature", &temp_c, current_idx)) return false;
  if (!json_extract_string(body, "icon", &icon, current_idx)) {
    icon = "Unknown";
  }

  *temp_c_out = temp_c;
  *icon_out = map_pirateweather_icon(icon);
  return true;
}

void poll_weather(uint32_t now_ms) {
  // If controller is providing weather via ESP-NOW, update globals and skip self-polling.
  if (g_runtime != nullptr && g_runtime->has_controller_weather()) {
    if (!g_have_weather_data || g_runtime->last_controller_weather_ms() == 0) {
      g_runtime->set_last_controller_weather_ms(now_ms);
      g_have_weather_data = true;
    }
    // Only fall back to self-polling if no controller weather for 30 minutes.
    constexpr uint32_t kControllerWeatherTimeoutMs = 30UL * 60UL * 1000UL;
    if ((now_ms - g_runtime->last_controller_weather_ms()) < kControllerWeatherTimeoutMs) {
      return;
    }
  }

  if (WiFi.status() != WL_CONNECTED) return;
  if (g_cfg_pirateweather_api_key.length() == 0 || g_cfg_pirateweather_zip.length() == 0) {
    g_have_weather_data = false;
    return;
  }
  if (g_last_weather_poll_ms != 0 && (now_ms - g_last_weather_poll_ms) < kWeatherPollMs) return;
  g_last_weather_poll_ms = now_ms;

  const String zip = normalize_zip(g_cfg_pirateweather_zip);
  if (zip.length() == 0) {
    g_have_weather_data = false;
    return;
  }

  float lat = 0.0f;
  float lon = 0.0f;
  if (!fetch_zip_coordinates(zip, &lat, &lon)) {
    g_have_weather_data = false;
    return;
  }

  float outdoor_temp_c = 0.0f;
  WeatherIcon icon = WeatherIcon::Unknown;
  if (!fetch_pirateweather_current(lat, lon, &outdoor_temp_c, &icon)) {
    g_have_weather_data = false;
    return;
  }

  g_outdoor_temp_c = outdoor_temp_c;
  g_weather_icon = icon;
  g_have_weather_data = true;
  if (g_runtime != nullptr) {
    g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_icon);
  }
}

bool parse_mac(const char *text, uint8_t out[6]) {
  if (text == nullptr || strlen(text) < 17) return false;
  unsigned values[6] = {};
  if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(values[i] & 0xFFu);
  return true;
}

bool is_broadcast_mac(const uint8_t mac[6]) {
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, kBroadcast, sizeof(kBroadcast)) == 0;
}

bool parse_lmk_hex(const char *text, uint8_t out[16]) {
  if (text == nullptr || strlen(text) != 32) return false;
  for (int i = 0; i < 16; ++i) {
    unsigned byte = 0;
    if (sscanf(text + (i * 2), "%02x", &byte) != 1) return false;
    out[i] = static_cast<uint8_t>(byte & 0xFFu);
  }
  return true;
}

const char *reset_reason_text(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deep_sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "other";
  }
}

String json_escape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

String html_escape(const String &in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c; break;
    }
  }
  return out;
}

void web_handle_config_get() {
  String body = "{";
  body += "\"wifi_ssid\":\"" + json_escape(g_cfg_wifi_ssid) + "\",";
  body += "\"wifi_password\":\"" + String(g_cfg_wifi_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_host\":\"" + json_escape(g_cfg_mqtt_host) + "\",";
  body += "\"mqtt_port\":" + String(g_cfg_mqtt_port) + ",";
  body += "\"mqtt_user\":\"" + json_escape(g_cfg_mqtt_user) + "\",";
  body += "\"mqtt_password\":\"" + String(g_cfg_mqtt_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_client_id\":\"" + json_escape(g_cfg_mqtt_client_id) + "\",";
  body += "\"mqtt_base_topic\":\"" + json_escape(g_cfg_mqtt_base_topic) + "\",";
  body += "\"discovery_prefix\":\"" + json_escape(g_cfg_discovery_prefix) + "\",";
  body += "\"shared_device_id\":\"" + json_escape(g_cfg_shared_device_id) + "\",";
  body += "\"pirateweather_api_key\":\"" +
          String(g_cfg_pirateweather_api_key.length() > 0 ? "set" : "unset") + "\",";
  body += "\"pirateweather_zip\":\"" + json_escape(g_cfg_pirateweather_zip) + "\",";
  body += "\"display_timeout_s\":" + String(g_display_timeout_ms / 1000UL) + ",";
  body += "\"backlight_active_pct\":" + String(g_cfg_backlight_active_pct) + ",";
  body += "\"backlight_screensaver_pct\":" + String(g_cfg_backlight_screensaver_pct) + ",";
  body += "\"temp_comp_c\":" + String(g_cfg_temp_comp_c, 2) + ",";
  body += "\"temperature_unit\":\"" + String(g_cfg_temp_unit_f ? "f" : "c") + "\",";
  body += "\"ota_hostname\":\"" + json_escape(g_cfg_ota_hostname) + "\",";
  body += "\"ota_password\":\"" + String(g_cfg_ota_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"espnow_channel\":" + String(g_cfg_espnow_channel) + ",";
  body += "\"espnow_peer_mac\":\"" + json_escape(g_cfg_espnow_peer_mac) + "\",";
  body += "\"espnow_lmk\":\"" + String(g_cfg_espnow_lmk.length() > 0 ? "set" : "unset") + "\",";
  body += "\"controller_base_topic\":\"" + json_escape(g_cfg_controller_base_topic) + "\",";
  body += "\"controller_timeout_ms\":" + String(g_cfg_controller_timeout_ms) + ",";
  body += "\"reboot_required\":" + String(g_cfg_reboot_required ? "true" : "false");
  body += "}";
  g_web.send(200, "application/json", body);
}

void web_handle_config_post() {
  int updated = 0;
  for (int i = 0; i < g_web.args(); ++i) {
    if (try_update_runtime_config(g_web.argName(i), g_web.arg(i).c_str())) {
      ++updated;
    }
  }
  g_web.send(200, "text/plain", "updated=" + String(updated) + "\n");
}

void web_handle_reboot_post() {
  schedule_reboot();
  g_web.send(200, "text/plain", "rebooting\n");
}

void write_u16_le(uint8_t *out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFu);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFu);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

void web_handle_screenshot() {
  // Read directly from the panel framebuffer that LVGL is NOT currently
  // drawing into (the one being displayed has a complete frame).
  if (g_buf_1 == nullptr || g_buf_2 == nullptr) {
    g_web.send(503, "text/plain", "display not initialized");
    return;
  }

  // Force a full refresh so both buffers are up to date, then read the
  // buffer that LVGL just finished drawing (the one now being displayed).
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);

  // After lv_refr_now the draw_buf->buf_act points to the buffer LVGL will
  // draw into *next*, so the *other* buffer is the freshly completed frame.
  const auto *fb = reinterpret_cast<const uint16_t *>(
      g_draw_buf.buf_act == g_buf_1 ? g_buf_2 : g_buf_1);

  const uint32_t row_size = ((kDisplayWidth * 3U) + 3U) & ~3U;
  const uint32_t image_size = row_size * kDisplayHeight;
  const uint32_t file_size = 54U + image_size;

  g_web.setContentLength(file_size);
  g_web.send(200, "image/bmp", "");
  WiFiClient client = g_web.client();

  uint8_t header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  write_u32_le(&header[2], file_size);
  write_u32_le(&header[10], 54U);
  write_u32_le(&header[14], 40U);
  write_u32_le(&header[18], static_cast<uint32_t>(kDisplayWidth));
  write_u32_le(&header[22], static_cast<uint32_t>(kDisplayHeight));
  write_u16_le(&header[26], 1U);
  write_u16_le(&header[28], 24U);
  write_u32_le(&header[34], image_size);
  client.write(header, sizeof(header));

  static uint8_t row[((kDisplayWidth * 3U) + 3U) & ~3U];
  for (int y = kDisplayHeight - 1; y >= 0; --y) {
    uint32_t idx = 0;
    for (int x = 0; x < kDisplayWidth; ++x) {
      const uint16_t px = fb[(y * kDisplayWidth) + x];
      const uint8_t r = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
      const uint8_t g = static_cast<uint8_t>(((px >> 5) & 0x3F) * 255 / 63);
      const uint8_t b = static_cast<uint8_t>((px & 0x1F) * 255 / 31);
      row[idx++] = b;
      row[idx++] = g;
      row[idx++] = r;
    }
    while (idx < row_size) {
      row[idx++] = 0;
    }
    client.write(row, row_size);
  }
}

void web_handle_root() {
  String html;
  html.reserve(8192);
  html += "<html><body><h1>Thermostat Display Config</h1>";
  html += "<p><a href=\"/config\">JSON config</a> | <a href=\"/screenshot\">Screenshot</a> | <a href=\"/update\">Firmware Update</a></p>";

  html += "<fieldset><legend>Networking Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"wifi_ssid\" maxlength=\"64\" value=\"" + html_escape(g_cfg_wifi_ssid) +
          "\"><br>";
  html += "wifi_password: <input name=\"wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"mqtt_host\" value=\"" + html_escape(g_cfg_mqtt_host) + "\"><br>";
  html += "mqtt_port: <input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" step=\"1\" value=\"" +
          String(g_cfg_mqtt_port) + "\"><br>";
  html += "mqtt_user: <input name=\"mqtt_user\" value=\"" + html_escape(g_cfg_mqtt_user) + "\"><br>";
  html += "mqtt_password: <input name=\"mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"mqtt_client_id\" value=\"" + html_escape(g_cfg_mqtt_client_id) + "\"><br>";
  html += "mqtt_base_topic: <input name=\"mqtt_base_topic\" value=\"" + html_escape(g_cfg_mqtt_base_topic) + "\"><br>";
  html += "espnow_channel: <input name=\"espnow_channel\" type=\"number\" min=\"1\" max=\"14\" step=\"1\" value=\"" +
          String(g_cfg_espnow_channel) + "\"><br>";
  html += "espnow_peer_mac: <input name=\"espnow_peer_mac\" pattern=\"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$\" title=\"Format: AA:BB:CC:DD:EE:FF\" value=\"" +
          html_escape(g_cfg_espnow_peer_mac) + "\"><br>";
  html += "espnow_lmk: <input name=\"espnow_lmk\" pattern=\"^[0-9A-Fa-f]{32}$\" title=\"32 hex characters\" value=\"\"><br>";
  html += "controller_base_topic: <input name=\"controller_base_topic\" value=\"" + html_escape(g_cfg_controller_base_topic) + "\"><br>";
  html += "<button type=\"submit\">Save Networking</button></form></fieldset>";

  html += "<fieldset><legend>Hardware Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "temp_comp_c: <input name=\"temp_comp_c\" type=\"number\" min=\"-10\" max=\"10\" step=\"0.01\" value=\"" +
          String(g_cfg_temp_comp_c, 2) + "\"><br>";
  html += "controller_timeout_ms: <input name=\"controller_timeout_ms\" type=\"number\" min=\"1000\" max=\"600000\" step=\"1\" value=\"" +
          String(g_cfg_controller_timeout_ms) + "\"><br>";
  html += "ota_hostname: <input name=\"ota_hostname\" value=\"" + html_escape(g_cfg_ota_hostname) + "\"><br>";
  html += "ota_password: <input name=\"ota_password\" value=\"\"><br>";
  html += "<button type=\"submit\">Save Hardware</button></form></fieldset>";

  html += "<fieldset><legend>Display Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "display_timeout_s: <input name=\"display_timeout_s\" type=\"number\" min=\"30\" max=\"600\" step=\"1\" value=\"" +
          String(g_display_timeout_ms / 1000UL) + "\"><br>";
  html += "backlight_active_pct: <input name=\"backlight_active_pct\" type=\"number\" min=\"0\" max=\"100\" step=\"1\" value=\"" +
          String(g_cfg_backlight_active_pct) + "\"><br>";
  html += "backlight_screensaver_pct: <input name=\"backlight_screensaver_pct\" type=\"number\" min=\"0\" max=\"100\" step=\"1\" value=\"" +
          String(g_cfg_backlight_screensaver_pct) + "\"><br>";
  html += "temperature_unit (c/f): <input name=\"temperature_unit\" pattern=\"^(c|f|celsius|fahrenheit)$\" title=\"Use c, f, celsius, or fahrenheit\" value=\"" +
          String(g_cfg_temp_unit_f ? "f" : "c") + "\"><br>";
  html += "<button type=\"submit\">Save Display</button></form></fieldset>";

  html += "<fieldset><legend>Weather</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "pirateweather_api_key: <input name=\"pirateweather_api_key\" value=\"\"><br>";
  html += "pirateweather_zip: <input name=\"pirateweather_zip\" pattern=\"^[0-9]{5}(-[0-9]{4})?$\" title=\"US ZIP format: 12345 or 12345-6789\" value=\"" +
          html_escape(g_cfg_pirateweather_zip) + "\"><br>";
  html += "<button type=\"submit\">Save Weather</button></form></fieldset>";

  html += "<fieldset><legend>Miscellaneous</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "discovery_prefix: <input name=\"discovery_prefix\" value=\"" + html_escape(g_cfg_discovery_prefix) +
          "\"><br>";
  html += "shared_device_id: <input name=\"shared_device_id\" pattern=\"^[A-Za-z0-9_-]{1,64}$\" title=\"1-64 chars: letters, numbers, underscore, hyphen\" value=\"" + html_escape(g_cfg_shared_device_id) +
          "\"><br>";
  html += "<button type=\"submit\">Save Misc</button></form></fieldset>";

  html += "<p>reboot_required=" + String(g_cfg_reboot_required ? "true" : "false") + "</p>";
  html += "<form method=\"post\" action=\"/reboot\"><button type=\"submit\">Reboot Display</button></form>";
  html += "<p><img src=\"/screenshot\" style=\"max-width:95vw;border:1px solid #ccc\"></p>";
  html += "</body></html>";
  g_web.send(200, "text/html", html);
}

void ensure_web_ready() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!g_web_started) {
    g_web.on("/", HTTP_GET, web_handle_root);
    g_web.on("/config", HTTP_GET, web_handle_config_get);
    g_web.on("/config", HTTP_POST, web_handle_config_post);
    g_web.on("/reboot", HTTP_POST, web_handle_reboot_post);
    g_web.on("/screenshot", HTTP_GET, web_handle_screenshot);
    ota_web_setup(g_web);
    g_web.begin();
    g_web_started = true;
  }
  g_web.handleClient();
}

std::string current_time_text() {
  time_t now = time(nullptr);
  if (now < 1700000000L) {
    return "--:--";
  }
  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%I:%M %p", &local_tm);
  return std::string(buf[0] == '0' ? buf + 1 : buf);
}

std::string current_date_text() {
  time_t now = time(nullptr);
  if (now < 1700000000L) {
    return "--- date ---";
  }
  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%A, %b %d", &local_tm);
  return std::string(buf);
}

void mqtt_publish_state() {
  if (g_runtime == nullptr || !g_mqtt.connected()) return;

  const uint32_t now = millis();

  g_mqtt.publish(topic_for("state/availability").c_str(), "online", true);
  g_mqtt.publish(topic_for("state/mode").c_str(), mode_to_mqtt(g_runtime->local_mode()), true);
  g_mqtt.publish(topic_for("state/fan_mode").c_str(), fan_to_mqtt(g_runtime->local_fan_mode()),
                 true);

  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", g_runtime->local_setpoint_c());
  g_mqtt.publish(topic_for("state/target_temp_c").c_str(), buf, true);
  if (g_runtime->has_last_packed_command()) {
    snprintf(buf, sizeof(buf), "%lu",
             static_cast<unsigned long>(g_runtime->last_packed_command()));
    g_mqtt.publish(topic_for("state/packed_command").c_str(), buf, true);
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_runtime->last_command_seq()));
    g_mqtt.publish(topic_for("state/command_seq").c_str(), buf, true);
  }

  snprintf(buf, sizeof(buf), "%.2f", g_runtime->local_temperature_compensation_c());
  g_mqtt.publish(topic_for("state/temp_comp_c").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_display_timeout_ms / 1000UL));
  g_mqtt.publish(topic_for("state/display_timeout_s").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_cfg_backlight_active_pct));
  g_mqtt.publish(topic_for("state/backlight_active_pct").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_cfg_backlight_screensaver_pct));
  g_mqtt.publish(topic_for("state/backlight_screensaver_pct").c_str(), buf, true);
  g_mqtt.publish(topic_for("state/firmware_version").c_str(), THERMOSTAT_FIRMWARE_VERSION, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_boot_count));
  g_mqtt.publish(topic_for("state/boot_count").c_str(), buf, true);
  g_mqtt.publish(topic_for("state/reset_reason").c_str(), g_reset_reason.c_str(), true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(millis() / 1000UL));
  g_mqtt.publish(topic_for("state/uptime_s").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_last_mqtt_command_ms));
  g_mqtt.publish(topic_for("state/last_mqtt_command_ms").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_runtime->last_controller_heartbeat_ms()));
  g_mqtt.publish(topic_for("state/last_espnow_rx_ms").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_runtime->espnow_send_ok_count()));
  g_mqtt.publish(topic_for("state/espnow_send_ok_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_runtime->espnow_send_fail_count()));
  g_mqtt.publish(topic_for("state/espnow_send_fail_count").c_str(), buf, true);
  if (g_last_espnow_error != "begin_failed") {
    g_last_espnow_error = g_runtime->espnow_send_fail_count() > 0 ? "send_failed" : "none";
  }
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(esp_get_free_heap_size()));
  g_mqtt.publish(topic_for("state/free_heap_bytes").c_str(), buf, true);
  g_mqtt.publish(topic_for("state/error_mqtt").c_str(), g_last_mqtt_error.c_str(), true);
  g_mqtt.publish(topic_for("state/error_ota").c_str(), g_last_ota_error.c_str(), true);
  g_mqtt.publish(topic_for("state/error_espnow").c_str(), g_last_espnow_error.c_str(), true);

  {
    const uint32_t now_ms = millis();
    const uint32_t mqtt_ms = g_last_mqtt_command_ms;
    const uint32_t espnow_ms = g_runtime->last_controller_heartbeat_ms();
    const bool mqtt_active = mqtt_ms > 0 && (now_ms - mqtt_ms) < g_cfg_controller_timeout_ms;
    const bool espnow_active = espnow_ms > 0 && (now_ms - espnow_ms) < g_cfg_controller_timeout_ms;
    const char *path = espnow_active && mqtt_active ? "mqtt+esp-now"
                       : mqtt_active                ? "mqtt"
                       : espnow_active              ? "esp-now"
                                                    : "disconnected";
    g_mqtt.publish(topic_for("state/connection_path").c_str(), path, true);
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_mqtt.publish(topic_for("state/wifi_ip").c_str(), WiFi.localIP().toString().c_str(), true);
    g_mqtt.publish(topic_for("state/wifi_mac").c_str(), WiFi.macAddress().c_str(), true);
    g_mqtt.publish(topic_for("state/wifi_ssid").c_str(), WiFi.SSID().c_str(), true);
    snprintf(buf, sizeof(buf), "%d", WiFi.channel());
    g_mqtt.publish(topic_for("state/wifi_channel").c_str(), buf, true);
    snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
    g_mqtt.publish(topic_for("state/wifi_rssi").c_str(), buf, true);
  }

  bool ok_temp = false;
  const float indoor_c = parse_numeric_prefix(g_runtime->indoor_temp_text(), &ok_temp);
  if (ok_temp) {
    snprintf(buf, sizeof(buf), "%.2f", indoor_c);
    g_mqtt.publish(topic_for("state/current_temp_c").c_str(), buf, true);
  }

  bool ok_humidity = false;
  const float indoor_h = parse_numeric_prefix(g_runtime->indoor_humidity_text(), &ok_humidity);
  if (ok_humidity) {
    snprintf(buf, sizeof(buf), "%.2f", indoor_h);
    g_mqtt.publish(topic_for("state/current_humidity").c_str(), buf, true);
  }

  // Publish sensor data to controller's topic namespace for MQTT-based intake
  if (g_cfg_controller_base_topic.length() > 0 && WiFi.status() == WL_CONNECTED) {
    String mac_str = WiFi.macAddress();
    if (ok_temp) {
      String topic = g_cfg_controller_base_topic + "/sensor/" + mac_str + "/temp_c";
      snprintf(buf, sizeof(buf), "%.2f", indoor_c);
      g_mqtt.publish(topic.c_str(), buf, false);
    }
    if (ok_humidity) {
      String topic = g_cfg_controller_base_topic + "/sensor/" + mac_str + "/humidity";
      snprintf(buf, sizeof(buf), "%.2f", indoor_h);
      g_mqtt.publish(topic.c_str(), buf, false);
    }
  }

  g_mqtt.publish(topic_for("state/status").c_str(), g_runtime->status_text(now).c_str(), true);
}

void mqtt_publish_discovery() {
  if (!g_mqtt.connected() || g_mqtt_discovery_sent) return;

  const String base = g_cfg_mqtt_base_topic;
  const String node = g_cfg_shared_device_id;

  // Climate entity is now published by the controller. Display publishes only
  // display-specific entities (timeout, brightness, diagnostics).

  char payload[1500];
  const String timeout_config = g_cfg_discovery_prefix + "/number/" + node +
                                "_display_timeout/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Timeout\",\"uniq_id\":\"%s_display_timeout\","
           "\"cmd_t\":\"%s/cmd/display_timeout_s\",\"stat_t\":\"%s/state/display_timeout_s\","
           "\"min\":30,\"max\":600,\"step\":5,\"mode\":\"box\",\"unit_of_meas\":\"s\","
           "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"],\"name\":\"Wireless Thermostat System\","
           "\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
           node.c_str(), base.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(timeout_config.c_str(), payload, true);

  const String brightness_config = g_cfg_discovery_prefix + "/number/" + node +
                                   "_display_backlight_active/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Brightness\",\"uniq_id\":\"%s_display_backlight_active\","
           "\"cmd_t\":\"%s/cmd/backlight_active_pct\","
           "\"stat_t\":\"%s/state/backlight_active_pct\","
           "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"%%\","
           "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(brightness_config.c_str(), payload, true);

  const String dim_brightness_config = g_cfg_discovery_prefix + "/number/" + node +
                                       "_display_backlight_screensaver/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Screensaver Brightness\","
           "\"uniq_id\":\"%s_display_backlight_screensaver\","
           "\"cmd_t\":\"%s/cmd/backlight_screensaver_pct\","
           "\"stat_t\":\"%s/state/backlight_screensaver_pct\","
           "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"%%\","
           "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(dim_brightness_config.c_str(), payload, true);

  const String fw_config = g_cfg_discovery_prefix + "/sensor/" + node + "_firmware/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Firmware Version\",\"uniq_id\":\"%s_firmware\",\"stat_t\":\"%s/state/firmware_version\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(fw_config.c_str(), payload, true);

  const String rssi_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_wifi_rssi/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display WiFi RSSI\",\"uniq_id\":\"%s_display_wifi_rssi\","
           "\"stat_t\":\"%s/state/wifi_rssi\",\"unit_of_meas\":\"dBm\","
           "\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(rssi_config.c_str(), payload, true);

  const String ip_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_ip/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display IP Address\",\"uniq_id\":\"%s_display_ip\","
           "\"stat_t\":\"%s/state/wifi_ip\",\"icon\":\"mdi:ip-network\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(ip_config.c_str(), payload, true);

  const String conn_path_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_connection_path/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Connection Path\",\"uniq_id\":\"%s_display_connection_path\","
           "\"stat_t\":\"%s/state/connection_path\",\"icon\":\"mdi:connection\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(conn_path_config.c_str(), payload, true);

  const String heap_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_free_heap/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Free Heap\",\"uniq_id\":\"%s_display_free_heap\","
           "\"stat_t\":\"%s/state/free_heap_bytes\",\"unit_of_meas\":\"B\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(heap_config.c_str(), payload, true);

  const String last_mqtt_cmd_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_last_mqtt_command/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Last MQTT Command\",\"uniq_id\":\"%s_display_last_mqtt_command\","
           "\"stat_t\":\"%s/state/last_mqtt_command_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(last_mqtt_cmd_config.c_str(), payload, true);

  const String last_espnow_rx_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_last_espnow_rx/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Last ESP-NOW RX\",\"uniq_id\":\"%s_display_last_espnow_rx\","
           "\"stat_t\":\"%s/state/last_espnow_rx_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(last_espnow_rx_config.c_str(), payload, true);

  const String espnow_ok_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_espnow_send_ok/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display ESP-NOW Send OK\",\"uniq_id\":\"%s_display_espnow_send_ok\","
           "\"stat_t\":\"%s/state/espnow_send_ok_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(espnow_ok_config.c_str(), payload, true);

  const String espnow_fail_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_espnow_send_fail/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display ESP-NOW Send Fail\",\"uniq_id\":\"%s_display_espnow_send_fail\","
           "\"stat_t\":\"%s/state/espnow_send_fail_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(espnow_fail_config.c_str(), payload, true);

  const String err_mqtt_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_error_mqtt/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display MQTT Error\",\"uniq_id\":\"%s_display_error_mqtt\","
           "\"stat_t\":\"%s/state/error_mqtt\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(err_mqtt_config.c_str(), payload, true);

  const String err_ota_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_error_ota/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display OTA Error\",\"uniq_id\":\"%s_display_error_ota\","
           "\"stat_t\":\"%s/state/error_ota\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(err_ota_config.c_str(), payload, true);

  const String err_espnow_config =
      g_cfg_discovery_prefix + "/sensor/" + node + "_display_error_espnow/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display ESP-NOW Error\",\"uniq_id\":\"%s_display_error_espnow\","
           "\"stat_t\":\"%s/state/error_espnow\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(err_espnow_config.c_str(), payload, true);

  const String reset_seq_config =
      g_cfg_discovery_prefix + "/button/" + node + "_display_reset_sequence/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Reset Command Sequence\","
           "\"uniq_id\":\"%s_display_reset_sequence\","
           "\"cmd_t\":\"%s/cmd/reset_sequence\",\"pl_prs\":\"1\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(reset_seq_config.c_str(), payload, true);

  const String reboot_config =
      g_cfg_discovery_prefix + "/button/" + node + "_display_reboot/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Reboot\","
           "\"uniq_id\":\"%s_display_reboot\","
           "\"cmd_t\":\"%s/cmd/reboot\",\"pl_prs\":\"1\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(reboot_config.c_str(), payload, true);

  g_mqtt_discovery_sent = true;
}

void mqtt_on_message(char *topic, uint8_t *payload, unsigned int length) {
  if (topic == nullptr || payload == nullptr) return;

  char value[128];
  const size_t copy_len = (length < sizeof(value) - 1) ? length : sizeof(value) - 1;
  memcpy(value, payload, copy_len);
  value[copy_len] = '\0';
  char normalized[128];
  memcpy(normalized, value, copy_len + 1);
  lower_in_place(normalized);

  const uint32_t now = millis();
  const String topic_str(topic);
  const String cfg_prefix = topic_for("cfg/");

  if (topic_str.startsWith(cfg_prefix) && topic_str.endsWith("/set")) {
    const int key_begin = static_cast<int>(cfg_prefix.length());
    const int key_end = static_cast<int>(topic_str.length()) - 4;
    if (key_end > key_begin) {
      const String key = topic_str.substring(key_begin, key_end);
      try_update_runtime_config(key, value);
    }
    return;
  }

  if (g_runtime == nullptr) return;
  if (topic_str.startsWith(topic_for("cmd/"))) {
    g_last_mqtt_command_ms = now;
  }

  if (topic_str == topic_for("cmd/mode")) {
    if (strcmp(normalized, "heat") == 0) {
      g_runtime->on_user_set_mode(FurnaceMode::Heat, now);
    } else if (strcmp(normalized, "cool") == 0) {
      g_runtime->on_user_set_mode(FurnaceMode::Cool, now);
    } else {
      g_runtime->on_user_set_mode(FurnaceMode::Off, now);
    }
  } else if (topic_str == topic_for("cmd/fan_mode")) {
    if (strcmp(normalized, "on") == 0 || strcmp(normalized, "always on") == 0) {
      g_runtime->on_user_set_fan_mode(FanMode::AlwaysOn, now);
    } else if (strcmp(normalized, "circulate") == 0) {
      g_runtime->on_user_set_fan_mode(FanMode::Circulate, now);
    } else {
      g_runtime->on_user_set_fan_mode(FanMode::Automatic, now);
    }
  } else if (topic_str == topic_for("cmd/target_temp_c")) {
    float celsius = static_cast<float>(atof(value));
    if (!std::isfinite(celsius)) return;
    if (celsius < 0.0f) celsius = 0.0f;
    if (celsius > 40.0f) celsius = 40.0f;
    g_runtime->on_user_set_setpoint_c(celsius, now);
  } else if (topic_str == topic_for("cmd/unit")) {
    g_cfg_temp_unit_f = strcmp(normalized, "f") == 0 || strcmp(normalized, "fahrenheit") == 0;
    g_runtime->set_temperature_unit(g_cfg_temp_unit_f ? TemperatureUnit::Fahrenheit
                                                      : TemperatureUnit::Celsius);
    if (g_cfg_ready) {
      g_cfg.putBool("temp_u_f", g_cfg_temp_unit_f);
    }
  } else if (topic_str == topic_for("cmd/sync")) {
    if (parse_bool_payload(normalized)) {
      g_runtime->request_sync(now);
    }
  } else if (topic_str == topic_for("cmd/reboot")) {
    if (parse_bool_payload(normalized)) {
      schedule_reboot();
    }
  } else if (topic_str == topic_for("cmd/reset_sequence")) {
    if (parse_bool_payload(normalized)) {
      g_runtime->reset_local_command_sequence();
      g_runtime->request_sync(now);
    }
  } else if (topic_str == topic_for("cmd/filter_reset")) {
    if (parse_bool_payload(normalized)) {
      g_runtime->request_filter_reset(now);
    }
  } else if (topic_str == topic_for("cmd/temp_comp_c")) {
    float comp = static_cast<float>(atof(value));
    if (!std::isfinite(comp)) return;
    if (comp < -10.0f) comp = -10.0f;
    if (comp > 10.0f) comp = 10.0f;
    g_cfg_temp_comp_c = comp;
    g_runtime->set_local_temperature_compensation_c(g_cfg_temp_comp_c);
    if (g_cfg_ready) {
      g_cfg.putFloat("temp_comp", g_cfg_temp_comp_c);
    }
  } else if (topic_str == topic_for("cmd/display_timeout_s")) {
    long seconds = atol(value);
    if (seconds < 30) seconds = 30;
    if (seconds > 600) seconds = 600;
    g_display_timeout_ms = static_cast<uint32_t>(seconds) * 1000UL;
    if (g_cfg_ready) {
      g_cfg.putUInt("disp_to_s", static_cast<uint32_t>(seconds));
    }
    g_screen.set_display_timeout_ms(g_display_timeout_ms);
  } else if (topic_str == topic_for("cmd/backlight_active_pct")) {
    g_cfg_backlight_active_pct = clamp_percent(atol(value));
    if (g_cfg_ready) {
      g_cfg.putUChar("bl_act", g_cfg_backlight_active_pct);
    }
  } else if (topic_str == topic_for("cmd/backlight_screensaver_pct")) {
    g_cfg_backlight_screensaver_pct = clamp_percent(atol(value));
    if (g_cfg_ready) {
      g_cfg.putUChar("bl_dim", g_cfg_backlight_screensaver_pct);
    }
  }

  // Controller state topics (MQTT-primary path)
  if (g_cfg_controller_base_topic.length() > 0) {
    if (topic_str == controller_topic_for("state/mode")) {
      g_mqtt_ctrl_mode = mqtt_payload::str_to_mode(normalized);
      g_mqtt_ctrl_last_update_ms = now;
    } else if (topic_str == controller_topic_for("state/fan_mode")) {
      g_mqtt_ctrl_fan = mqtt_payload::str_to_fan(normalized);
      g_mqtt_ctrl_last_update_ms = now;
    } else if (topic_str == controller_topic_for("state/target_temp_c")) {
      float sp = static_cast<float>(atof(value));
      if (std::isfinite(sp)) {
        g_mqtt_ctrl_setpoint_c = sp;
        g_mqtt_ctrl_last_update_ms = now;
      }
    } else if (topic_str == controller_topic_for("state/furnace_state")) {
      g_mqtt_ctrl_state = static_cast<FurnaceStateCode>(atoi(value));
      g_mqtt_ctrl_last_update_ms = now;
    } else if (topic_str == controller_topic_for("state/lockout")) {
      g_mqtt_ctrl_lockout = mqtt_payload::parse_bool(value);
      g_mqtt_ctrl_last_update_ms = now;
    } else if (topic_str == controller_topic_for("state/filter_runtime_hours")) {
      float hours = static_cast<float>(atof(value));
      if (std::isfinite(hours) && hours >= 0.0f) {
        g_mqtt_ctrl_filter_runtime_s = static_cast<uint32_t>(hours * 3600.0f);
        g_mqtt_ctrl_last_update_ms = now;
      }
    } else if (topic_str == controller_topic_for("state/outdoor_temp_c")) {
      float temp = static_cast<float>(atof(value));
      if (std::isfinite(temp) && g_runtime != nullptr) {
        g_outdoor_temp_c = temp;
        g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_icon);
        g_have_weather_data = true;
      }
    } else if (topic_str == controller_topic_for("state/weather_condition")) {
      WeatherIcon icon = weather_icon_from_api(normalized);
      if (icon == WeatherIcon::Unknown) {
        // Try matching display text for conditions published as English names
        for (uint8_t i = 0; i <= static_cast<uint8_t>(WeatherIcon::Unknown); ++i) {
          const auto candidate = static_cast<WeatherIcon>(i);
          char lower_display[32];
          const char *display = weather_icon_display_text(candidate);
          size_t j = 0;
          for (; display[j] != '\0' && j < sizeof(lower_display) - 1; ++j) {
            lower_display[j] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(display[j])));
          }
          lower_display[j] = '\0';
          if (strcmp(normalized, lower_display) == 0) {
            icon = candidate;
            break;
          }
        }
      }
      if (g_runtime != nullptr) {
        g_weather_icon = icon;
        g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_icon);
        g_have_weather_data = true;
      }
    } else if (topic_str == controller_topic_for("state/availability")) {
      g_mqtt_ctrl_available = strcmp(normalized, "online") == 0;
    }
  }

  mqtt_publish_state();
}

void start_wifi_provisioning() {
  if (g_wifi_provisioning_started) return;
#ifdef IMPROV_WIFI_BLE_ENABLED
  ImprovBleConfig cfg = {};
  cfg.device_name = "Thermostat";
  cfg.firmware_name = THERMOSTAT_PROJECT_NAME;
  cfg.firmware_version = THERMOSTAT_FIRMWARE_VERSION;
  cfg.hardware_variant = "ESP32-S3";
  cfg.device_url = nullptr;
  improv_ble_start(cfg, [](const char *ssid, const char *password) {
    g_cfg_wifi_ssid = ssid;
    g_cfg_wifi_password = password;
    g_cfg.putString("wifi_ssid", ssid);
    g_cfg.putString("wifi_pwd", password);
    g_cfg_wifi_reconnect_required = true;
  });
#endif
  g_wifi_provisioning_started = true;
}

void wifi_event_handler(arduino_event_t *event) {
  if (event == nullptr) return;
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#ifdef IMPROV_WIFI_BLE_ENABLED
      if (improv_ble_is_active()) {
        improv_ble_stop();
      }
#endif
      g_wifi_provisioning_started = false;
      break;
    default:
      break;
  }
}

void ensure_wifi_connected(uint32_t now_ms) {
  if (g_cfg_wifi_reconnect_required) {
    g_cfg_wifi_reconnect_required = false;
    WiFi.disconnect();
    g_last_wifi_attempt_ms = 0;
  }
  if (WiFi.status() == WL_CONNECTED) return;

  if (g_cfg_wifi_ssid.length() > 0) {
    if ((now_ms - g_last_wifi_attempt_ms) < kNetworkRetryMs) return;
    g_last_wifi_attempt_ms = now_ms;
    WiFi.begin(g_cfg_wifi_ssid.c_str(), g_cfg_wifi_password.c_str());
    return;
  }

  if (g_wifi_provisioning_started) return;
  if (!g_wifi_has_attempted_stored_connect) {
    g_wifi_has_attempted_stored_connect = true;
    g_first_wifi_attempt_ms = now_ms;
    g_last_wifi_attempt_ms = now_ms;
    WiFi.begin();
    return;
  }

  if ((now_ms - g_first_wifi_attempt_ms) >= kProvisionStartDelayMs) {
    start_wifi_provisioning();
    return;
  }

  if ((now_ms - g_last_wifi_attempt_ms) < kNetworkRetryMs) return;
  g_last_wifi_attempt_ms = now_ms;
  WiFi.begin();
}

void ensure_mqtt_connected(uint32_t now_ms) {
  if (g_cfg_mqtt_host.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (g_cfg_mqtt_reconfigure_required) {
    g_cfg_mqtt_reconfigure_required = false;
    g_mqtt_discovery_sent = false;
    g_mqtt.disconnect();
  }
  if (g_mqtt.connected()) return;
  if ((now_ms - g_last_mqtt_attempt_ms) < kNetworkRetryMs) return;

  g_last_mqtt_attempt_ms = now_ms;
  g_mqtt.setServer(g_cfg_mqtt_host.c_str(), g_cfg_mqtt_port);

  bool ok = false;
  if (g_cfg_mqtt_user.length() == 0) {
    ok = g_mqtt.connect(g_cfg_mqtt_client_id.c_str());
  } else {
    ok = g_mqtt.connect(g_cfg_mqtt_client_id.c_str(), g_cfg_mqtt_user.c_str(),
                        g_cfg_mqtt_password.c_str());
  }

  if (!ok) {
    g_last_mqtt_error = String("connect_state_") + String(g_mqtt.state());
    return;
  }
  g_last_mqtt_error = "none";

  g_mqtt.subscribe(topic_for("cmd/mode").c_str());
  g_mqtt.subscribe(topic_for("cmd/fan_mode").c_str());
  g_mqtt.subscribe(topic_for("cmd/target_temp_c").c_str());
  g_mqtt.subscribe(topic_for("cmd/unit").c_str());
  g_mqtt.subscribe(topic_for("cmd/sync").c_str());
  g_mqtt.subscribe(topic_for("cmd/reboot").c_str());
  g_mqtt.subscribe(topic_for("cmd/reset_sequence").c_str());
  g_mqtt.subscribe(topic_for("cmd/filter_reset").c_str());
  g_mqtt.subscribe(topic_for("cmd/temp_comp_c").c_str());
  g_mqtt.subscribe(topic_for("cmd/display_timeout_s").c_str());
  g_mqtt.subscribe(topic_for("cmd/backlight_active_pct").c_str());
  g_mqtt.subscribe(topic_for("cmd/backlight_screensaver_pct").c_str());
  g_mqtt.subscribe(topic_for("cfg/+/set").c_str());

  if (g_cfg_controller_base_topic.length() > 0) {
    g_mqtt.subscribe(controller_topic_for("state/mode").c_str());
    g_mqtt.subscribe(controller_topic_for("state/fan_mode").c_str());
    g_mqtt.subscribe(controller_topic_for("state/target_temp_c").c_str());
    g_mqtt.subscribe(controller_topic_for("state/furnace_state").c_str());
    g_mqtt.subscribe(controller_topic_for("state/lockout").c_str());
    g_mqtt.subscribe(controller_topic_for("state/filter_runtime_hours").c_str());
    g_mqtt.subscribe(controller_topic_for("state/outdoor_temp_c").c_str());
    g_mqtt.subscribe(controller_topic_for("state/weather_condition").c_str());
    g_mqtt.subscribe(controller_topic_for("state/availability").c_str());
  }

  mqtt_publish_discovery();
  publish_all_cfg_state();
  mqtt_publish_state();
}

void ensure_ota_ready() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!g_ota_started) {
    ArduinoOTA.setHostname(g_cfg_ota_hostname.c_str());
    if (g_cfg_ota_password.length() > 0) {
      ArduinoOTA.setPassword(g_cfg_ota_password.c_str());
    }
    ArduinoOTA.onError([](ota_error_t error) {
      g_last_ota_error = String("ota_error_") + String(static_cast<unsigned>(error));
    });
    ArduinoOTA.onEnd([]() { g_last_ota_error = "none"; });
    ArduinoOTA.begin();
    g_ota_started = true;
  }
  ArduinoOTA.handle();
}

void ensure_mdns_ready() {
  if (WiFi.status() != WL_CONNECTED || g_mdns_started) return;
  const char *host =
      g_cfg_ota_hostname.length() > 0 ? g_cfg_ota_hostname.c_str() : "furnace-display";
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    g_mdns_started = true;
  }
}

void rgb_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  if (g_panel != nullptr) {
    // Drain any stale VSYNC signals so the take below waits for the
    // VSYNC that happens *after* draw_bitmap queues the buffer swap.
    xSemaphoreTake(g_flush_ready_sem, 0);

    esp_lcd_rgb_panel_restart(g_panel);
    esp_lcd_panel_draw_bitmap(g_panel, 0, 0, kDisplayWidth, kDisplayHeight, color_p);

    // Wait for the buffer swap to actually complete at VSYNC before
    // letting LVGL draw into the next buffer.
    xSemaphoreTake(g_flush_ready_sem, pdMS_TO_TICKS(100));
  }
  lv_disp_flush_ready(disp_drv);
}

bool gt911_read(uint16_t reg, uint8_t *buf, size_t len) {
  g_touch_i2c.beginTransmission(kGt911Addr);
  g_touch_i2c.write(static_cast<uint8_t>((reg >> 8) & 0xFF));
  g_touch_i2c.write(static_cast<uint8_t>(reg & 0xFF));
  if (g_touch_i2c.endTransmission(false) != 0) {
    return false;
  }

  const size_t read =
      g_touch_i2c.requestFrom(static_cast<int>(kGt911Addr), static_cast<int>(len));
  if (read != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    buf[i] = g_touch_i2c.read();
  }
  return true;
}

bool gt911_write(uint16_t reg, uint8_t val) {
  g_touch_i2c.beginTransmission(kGt911Addr);
  g_touch_i2c.write(static_cast<uint8_t>((reg >> 8) & 0xFF));
  g_touch_i2c.write(static_cast<uint8_t>(reg & 0xFF));
  g_touch_i2c.write(val);
  return g_touch_i2c.endTransmission() == 0;
}

void show_page(ThermostatPage page);
void apply_backlight(bool screensaver_active);

void poll_touch() {
  uint8_t status = 0;
  if (!gt911_read(0x814E, &status, 1)) {
    g_touch.touched = false;
    return;
  }

  const uint8_t points = status & 0x0F;
  if ((status & 0x80) == 0 || points == 0) {
    g_touch.touched = false;
    gt911_write(0x814E, 0);
    return;
  }

  uint8_t data[8] = {0};
  if (!gt911_read(0x8150, data, sizeof(data))) {
    g_touch.touched = false;
    return;
  }

  const uint16_t raw_x = static_cast<uint16_t>(data[1] << 8 | data[0]);
  const uint16_t raw_y = static_cast<uint16_t>(data[3] << 8 | data[2]);

  // Scale touch coordinates to display resolution if GT911 config differs
  const int16_t x = (g_touch_x_max != kDisplayWidth)
                        ? static_cast<int16_t>(static_cast<uint32_t>(raw_x) * kDisplayWidth / g_touch_x_max)
                        : static_cast<int16_t>(raw_x);
  const int16_t y = (g_touch_y_max != kDisplayHeight)
                        ? static_cast<int16_t>(static_cast<uint32_t>(raw_y) * kDisplayHeight / g_touch_y_max)
                        : static_cast<int16_t>(raw_y);

  g_touch.touched = true;
  g_touch.x = x;
  g_touch.y = y;

  gt911_write(0x814E, 0);
}

void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  poll_touch();
  if (g_touch.touched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = g_touch.x;
    data->point.y = g_touch.y;
    const bool was_screensaver = g_screen.screensaver_active();
    g_screen.on_user_interaction(millis());
    if (was_screensaver && !g_screen.screensaver_active()) {
      show_page(g_screen.current_page());
      apply_backlight(false);
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void show_page(ThermostatPage page) {
  if (g_home_page == nullptr) return;

  lv_obj_add_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);

  switch (page) {
    case ThermostatPage::Home:
      lv_obj_clear_flag(g_home_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case ThermostatPage::Fan:
      lv_obj_clear_flag(g_fan_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case ThermostatPage::Mode:
      lv_obj_clear_flag(g_mode_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case ThermostatPage::Settings:
      lv_obj_clear_flag(g_settings_page, LV_OBJ_FLAG_HIDDEN);
      break;
    case ThermostatPage::Screensaver:
      lv_obj_clear_flag(g_screensaver_page, LV_OBJ_FLAG_HIDDEN);
      break;
  }

  if (g_tabs != nullptr) {
    if (page == ThermostatPage::Screensaver) {
      lv_obj_add_flag(g_tabs, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(g_tabs, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void apply_backlight(bool screensaver_active) {
  if (!g_backlight_enabled) {
    ledcWrite(kBacklightPin, 0);
    return;
  }
  const uint8_t duty =
      screensaver_active ? percent_to_duty(g_cfg_backlight_screensaver_pct)
                         : percent_to_duty(g_cfg_backlight_active_pct);
  ledcWrite(kBacklightPin, duty);
}

void refresh_ui() {
  if (g_runtime == nullptr || g_status_label == nullptr) return;

  const uint32_t now = millis();

  lv_label_set_text(g_status_label, g_runtime->status_text(now).c_str());
  lv_label_set_text(g_indoor_label, g_runtime->indoor_temp_text().c_str());
  lv_label_set_text(g_humidity_label, g_runtime->indoor_humidity_text().c_str());
  lv_label_set_text(g_setpoint_label, g_runtime->setpoint_text().c_str());
  lv_label_set_text(g_weather_label, g_have_weather_data ? g_runtime->weather_text().c_str() : "");
  if (g_weather_icon_label != nullptr && g_have_weather_data) {
    lv_label_set_text(g_weather_icon_label,
                      thermostat::ui::weather_icon_symbol(g_runtime->weather_icon()));
  }

  if (g_home_time_label != nullptr) {
    lv_label_set_text(g_home_time_label, current_time_text().c_str());
  }
  if (g_home_date_label != nullptr) {
    lv_label_set_text(g_home_date_label, current_date_text().c_str());
  }
  lv_label_set_text(g_screen_time_label, current_time_text().c_str());
  if (g_screen_weather_label != nullptr) {
    lv_label_set_text(g_screen_weather_label, g_have_weather_data ? g_runtime->weather_text().c_str() : "");
  }
  if (g_screen_indoor_label != nullptr) {
    lv_label_set_text(g_screen_indoor_label, g_runtime->indoor_temp_text().c_str());
  }
  if (g_screen.screensaver_active()) {
    thermostat::ui::update_screensaver_layout(g_screen_time_label, g_screen_weather_label,
                                              g_screen_indoor_label, now / 60000UL);
  }
  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  const bool mqtt_connected = g_mqtt.connected();
  const uint32_t uptime_s = now / 1000UL;
  const uint32_t last_mqtt_ms = g_last_mqtt_command_ms;
  const uint32_t last_ctrl_hb_ms = g_runtime->last_controller_heartbeat_ms();
  const uint32_t since_mqtt_ms = last_mqtt_ms > 0 ? (now - last_mqtt_ms) : 0;
  const uint32_t since_ctrl_ms = last_ctrl_hb_ms > 0 ? (now - last_ctrl_hb_ms) : 0;

  String system_text;
  system_text.reserve(256);
  system_text += "fw: ";
  system_text += THERMOSTAT_FIRMWARE_VERSION;
  system_text += "\nboot_count: ";
  system_text += String(g_boot_count);
  system_text += "\nreset: ";
  system_text += g_reset_reason;
  system_text += "\nuptime_s: ";
  system_text += String(uptime_s);
  system_text += "\nfree_heap_b: ";
  system_text += String(static_cast<unsigned long>(esp_get_free_heap_size()));

  String wifi_text;
  wifi_text.reserve(256);
  wifi_text += "connected: ";
  wifi_text += wifi_connected ? "yes" : "no";
  wifi_text += "\nip: ";
  wifi_text += wifi_connected ? WiFi.localIP().toString() : String("N/A");
  wifi_text += "\nssid: ";
  wifi_text += wifi_connected ? WiFi.SSID() : String("N/A");
  wifi_text += "\nmac: ";
  wifi_text += WiFi.macAddress();
  wifi_text += "\nchannel: ";
  wifi_text += String(wifi_connected ? WiFi.channel() : 0);
  wifi_text += "\nrssi_dbm: ";
  wifi_text += String(wifi_connected ? WiFi.RSSI() : 0);

  String mqtt_text;
  mqtt_text.reserve(300);
  mqtt_text += "connected: ";
  mqtt_text += mqtt_connected ? "yes" : "no";
  mqtt_text += "\nstate: ";
  mqtt_text += String(g_mqtt.state());
  mqtt_text += "\nhost: ";
  mqtt_text += g_cfg_mqtt_host;
  mqtt_text += ":";
  mqtt_text += String(g_cfg_mqtt_port);
  mqtt_text += "\nclient_id: ";
  mqtt_text += g_cfg_mqtt_client_id;
  mqtt_text += "\nbase_topic: ";
  mqtt_text += g_cfg_mqtt_base_topic;
  mqtt_text += "\nuser: ";
  mqtt_text += g_cfg_mqtt_user.length() > 0 ? g_cfg_mqtt_user : String("(none)");
  mqtt_text += "\npassword: ";
  mqtt_text += g_cfg_mqtt_password.length() > 0 ? "set" : "unset";
  mqtt_text += "\nlast_cmd_ms: ";
  mqtt_text += String(last_mqtt_ms);
  mqtt_text += "\nsince_cmd_ms: ";
  mqtt_text += String(since_mqtt_ms);

  String controller_text;
  controller_text.reserve(128);
  controller_text += "last_hb_ms: ";
  controller_text += String(last_ctrl_hb_ms);
  controller_text += "\nsince_hb_ms: ";
  controller_text += String(since_ctrl_ms);

  String espnow_text;
  espnow_text.reserve(180);
  espnow_text += "channel: ";
  espnow_text += String(g_cfg_espnow_channel);
  espnow_text += "\npeer_mac: ";
  espnow_text += g_cfg_espnow_peer_mac;
  espnow_text += "\ntx_ok: ";
  espnow_text += String(static_cast<unsigned long>(g_runtime->espnow_send_ok_count()));
  espnow_text += "\ntx_fail: ";
  espnow_text += String(static_cast<unsigned long>(g_runtime->espnow_send_fail_count()));

  String config_text;
  config_text.reserve(220);
  config_text += "temp_unit: ";
  config_text += g_cfg_temp_unit_f ? "F" : "C";
  config_text += "\ntemp_comp_c: ";
  config_text += String(g_cfg_temp_comp_c, 2);
  config_text += "\ndisplay_timeout_s: ";
  config_text += String(g_display_timeout_ms / 1000UL);
  config_text += "\nbrightness_pct: ";
  config_text += String(g_cfg_backlight_active_pct);
  config_text += "\nsaver_pct: ";
  config_text += String(g_cfg_backlight_screensaver_pct);
  config_text += "\nreboot_required: ";
  config_text += g_cfg_reboot_required ? "true" : "false";
  config_text += "\nreboot_pending: ";
  config_text += g_reboot_requested ? "true" : "false";

  String errors_text;
  errors_text.reserve(160);
  errors_text += "mqtt: ";
  errors_text += g_last_mqtt_error;
  errors_text += "\nota: ";
  errors_text += g_last_ota_error;
  errors_text += "\nespnow: ";
  errors_text += g_last_espnow_error;

  if (g_settings_system_label != nullptr) lv_label_set_text(g_settings_system_label, system_text.c_str());
  if (g_settings_wifi_label != nullptr) lv_label_set_text(g_settings_wifi_label, wifi_text.c_str());
  if (g_settings_mqtt_label != nullptr) lv_label_set_text(g_settings_mqtt_label, mqtt_text.c_str());
  if (g_settings_controller_label != nullptr) {
    lv_label_set_text(g_settings_controller_label, controller_text.c_str());
  }
  if (g_settings_espnow_label != nullptr) lv_label_set_text(g_settings_espnow_label, espnow_text.c_str());
  if (g_settings_config_label != nullptr) lv_label_set_text(g_settings_config_label, config_text.c_str());
  if (g_settings_errors_label != nullptr) lv_label_set_text(g_settings_errors_label, errors_text.c_str());
  if (g_settings_display_label != nullptr) lv_label_set_text(g_settings_display_label, system_text.c_str());
  if (g_settings_diag_label != nullptr) lv_label_set_text(g_settings_diag_label, errors_text.c_str());
  if (g_timeout_slider != nullptr && !lv_obj_has_state(g_timeout_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_timeout_slider, static_cast<int32_t>(g_display_timeout_ms / 1000UL),
                        LV_ANIM_OFF);
  }
  if (g_brightness_slider != nullptr && !lv_obj_has_state(g_brightness_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_brightness_slider, static_cast<int32_t>(g_cfg_backlight_active_pct),
                        LV_ANIM_OFF);
  }
  if (g_dim_slider != nullptr && !lv_obj_has_state(g_dim_slider, LV_STATE_PRESSED)) {
    lv_slider_set_value(g_dim_slider, static_cast<int32_t>(g_cfg_backlight_screensaver_pct),
                        LV_ANIM_OFF);
  }

  thermostat::ui::set_mode_button_state(g_runtime->local_mode());
  thermostat::ui::set_fan_button_state(g_runtime->local_fan_mode());
  thermostat::ui::set_temperature_unit_button_state(g_runtime->temperature_unit());
  g_screen.on_mode_changed(g_runtime->local_mode());
  if (g_setpoint_column != nullptr) {
    if (g_screen.setpoint_visible()) {
      lv_obj_clear_flag(g_setpoint_column, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_setpoint_column, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_filter_label != nullptr) {
    if (g_runtime->filter_runtime_hours() >= kFilterChangeThresholdHours) {
      lv_label_set_text(g_filter_label, "Change Filter");
      lv_obj_clear_flag(g_filter_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_filter_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Fan page status
  if (g_fan_status_label != nullptr) {
    const FurnaceStateCode cs = g_runtime->controller_state();
    const char *fan_text = (cs == FurnaceStateCode::HeatOn || cs == FurnaceStateCode::CoolOn ||
                            cs == FurnaceStateCode::FanOn)
                               ? "Fan Running"
                               : "Idle";
    lv_label_set_text(g_fan_status_label, fan_text);
  }

  // Mode page status
  if (g_mode_status_label != nullptr) {
    lv_label_set_text(g_mode_status_label, g_runtime->status_text(now).c_str());
  }
}

void tab_event_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  const char *txt = lv_btnmatrix_get_btn_text(g_tabs, lv_btnmatrix_get_selected_btn(g_tabs));
  const uint32_t now = millis();
  if (strcmp(txt, "HOME") == 0) {
    g_screen.on_tab_selected(ThermostatPage::Home, now);
  } else if (strcmp(txt, "FAN") == 0) {
    g_screen.on_tab_selected(ThermostatPage::Fan, now);
  } else if (strcmp(txt, "MODE") == 0) {
    g_screen.on_tab_selected(ThermostatPage::Mode, now);
  } else if (strcmp(txt, "SETTINGS") == 0) {
    g_screen.on_tab_selected(ThermostatPage::Settings, now);
  }
  show_page(g_screen.current_page());
}

void btn_setpoint_up_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  const float step = (g_runtime->temperature_unit() == TemperatureUnit::Fahrenheit) ? 1.0f : 0.5f;
  const float user_val = g_runtime->temperature_unit() == TemperatureUnit::Fahrenheit
                             ? ((g_runtime->local_setpoint_c() * 9.0f / 5.0f) + 32.0f)
                             : g_runtime->local_setpoint_c();
  g_runtime->on_user_set_setpoint(user_val + step, millis());
}

void btn_setpoint_down_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  const float step = (g_runtime->temperature_unit() == TemperatureUnit::Fahrenheit) ? 1.0f : 0.5f;
  const float user_val = g_runtime->temperature_unit() == TemperatureUnit::Fahrenheit
                             ? ((g_runtime->local_setpoint_c() * 9.0f / 5.0f) + 32.0f)
                             : g_runtime->local_setpoint_c();
  g_runtime->on_user_set_setpoint(user_val - step, millis());
}

void btn_mode_cb(lv_event_t *e) {
  if (g_runtime == nullptr) return;
  auto mode = static_cast<FurnaceMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_runtime->on_user_set_mode(mode, millis());
}

void btn_fan_cb(lv_event_t *e) {
  if (g_runtime == nullptr) return;
  auto mode = static_cast<FanMode>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_runtime->on_user_set_fan_mode(mode, millis());
}

void btn_unit_cb(lv_event_t *e) {
  auto unit = static_cast<TemperatureUnit>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const char *raw = (unit == TemperatureUnit::Fahrenheit) ? "f" : "c";
  try_update_runtime_config("temperature_unit", raw);
  mqtt_publish_state();
}

void btn_sync_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  g_runtime->request_sync(millis());
}

void btn_filter_reset_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  g_runtime->request_filter_reset(millis());
}

void apply_runtime_u32_setting(const char *key, uint32_t value) {
  String raw(value);
  if (try_update_runtime_config(key, raw.c_str())) {
    mqtt_publish_state();
  }
}

uint32_t snap_to_step(uint32_t value, uint32_t step, uint32_t min_v, uint32_t max_v) {
  if (value < min_v) value = min_v;
  if (value > max_v) value = max_v;
  const uint32_t snapped = ((value + (step / 2U)) / step) * step;
  if (snapped < min_v) return min_v;
  if (snapped > max_v) return max_v;
  return snapped;
}

void timeout_slider_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  uint32_t seconds = static_cast<uint32_t>(lv_slider_get_value(slider));
  seconds = snap_to_step(seconds, 30U, 30U, 600U);
  lv_slider_set_value(slider, static_cast<int32_t>(seconds), LV_ANIM_OFF);
  apply_runtime_u32_setting("display_timeout_s", seconds);
}

void brightness_slider_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  uint32_t percent = static_cast<uint32_t>(lv_slider_get_value(slider));
  percent = snap_to_step(percent, 5U, 0U, 100U);
  lv_slider_set_value(slider, static_cast<int32_t>(percent), LV_ANIM_OFF);
  apply_runtime_u32_setting("backlight_active_pct", percent);
}

void dim_slider_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  if (slider == nullptr) return;
  uint32_t percent = static_cast<uint32_t>(lv_slider_get_value(slider));
  percent = snap_to_step(percent, 5U, 0U, 100U);
  lv_slider_set_value(slider, static_cast<int32_t>(percent), LV_ANIM_OFF);
  apply_runtime_u32_setting("backlight_screensaver_pct", percent);
}

void create_ui() {
  thermostat::ui::UiCallbacks callbacks{};
  callbacks.on_tab_changed = tab_event_cb;
  callbacks.on_setpoint_up = btn_setpoint_up_cb;
  callbacks.on_setpoint_down = btn_setpoint_down_cb;
  callbacks.on_mode = btn_mode_cb;
  callbacks.on_fan = btn_fan_cb;
  callbacks.on_unit = btn_unit_cb;
  callbacks.on_sync = btn_sync_cb;
  callbacks.on_filter_reset = btn_filter_reset_cb;
  callbacks.on_timeout_slider = timeout_slider_cb;
  callbacks.on_brightness_slider = brightness_slider_cb;
  callbacks.on_dim_slider = dim_slider_cb;

  thermostat::ui::UiHandles handles{};
  thermostat::ui::build_thermostat_ui(callbacks, &handles);

  g_tabs = handles.tabs;
  g_home_page = handles.home_page;
  g_fan_page = handles.fan_page;
  g_mode_page = handles.mode_page;
  g_settings_page = handles.settings_page;
  g_screensaver_page = handles.screensaver_page;
  g_status_label = handles.status_label;
  g_indoor_label = handles.indoor_label;
  g_humidity_label = handles.humidity_label;
  g_setpoint_label = handles.setpoint_label;
  g_weather_label = handles.weather_label;
  g_weather_icon_label = handles.weather_icon_label;
  g_home_time_label = handles.home_time_label;
  g_home_date_label = handles.home_date_label;
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
  g_setpoint_column = handles.setpoint_column;
  g_filter_label = handles.filter_label;
  g_fan_status_label = handles.fan_status_label;
  g_mode_status_label = handles.mode_status_label;

  show_page(ThermostatPage::Home);
}

void init_display_and_lvgl() {
  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
  panel_config.timings.pclk_hz = 14000000;
  panel_config.timings.h_res = kDisplayWidth;
  panel_config.timings.v_res = kDisplayHeight;
  panel_config.timings.hsync_back_porch = 8;
  panel_config.timings.hsync_front_porch = 8;
  panel_config.timings.hsync_pulse_width = 4;
  panel_config.timings.vsync_back_porch = 8;
  panel_config.timings.vsync_front_porch = 8;
  panel_config.timings.vsync_pulse_width = 4;
  panel_config.timings.flags.pclk_active_neg = 1;

  panel_config.data_width = 16;
  panel_config.de_gpio_num = 40;
  panel_config.pclk_gpio_num = 42;
  panel_config.vsync_gpio_num = 41;
  panel_config.hsync_gpio_num = 39;
  panel_config.disp_gpio_num = -1;

  // Pin order: R0-R4, G0-G5, B0-B4 (panel uses BGR element order)
  int pins[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
  for (size_t i = 0; i < 16; ++i) {
    panel_config.data_gpio_nums[i] = pins[i];
  }

  panel_config.flags.fb_in_psram = 1;
  panel_config.bounce_buffer_size_px = 10 * kDisplayWidth;
  panel_config.num_fbs = 2;

  delay(kDisplayInitSettleMs);
  if (esp_lcd_new_rgb_panel(&panel_config, &g_panel) == ESP_OK) {
    esp_lcd_panel_reset(g_panel);
    esp_lcd_panel_init(g_panel);

    // Register VSYNC callback so the flush can wait for the buffer swap
    g_flush_ready_sem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = on_vsync_ready;
    esp_lcd_rgb_panel_register_event_callbacks(g_panel, &cbs, nullptr);
  }

  // Get the panel's two PSRAM framebuffers for double-buffered direct mode.
  // LVGL draws dirty areas into one buffer while the panel DMA reads the other.
  // On flush, esp_lcd_panel_draw_bitmap detects the source is a panel buffer
  // and does an atomic pointer swap instead of a copy.
  void *panel_fb0 = nullptr;
  void *panel_fb1 = nullptr;
  esp_lcd_rgb_panel_get_frame_buffer(g_panel, 2, &panel_fb0, &panel_fb1);

  lv_init();

  const size_t buf_pixels = kDisplayWidth * kDisplayHeight;
  g_buf_1 = static_cast<lv_color_t *>(panel_fb0);
  g_buf_2 = static_cast<lv_color_t *>(panel_fb1);

  lv_disp_draw_buf_init(&g_draw_buf, g_buf_1, g_buf_2, buf_pixels);
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kDisplayWidth;
  g_disp_drv.ver_res = kDisplayHeight;
  g_disp_drv.flush_cb = rgb_flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  g_disp_drv.direct_mode = 1;
  g_disp_drv.full_refresh = 1;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&g_indev_drv);
}

void init_backlight() {
  ledcAttach(kBacklightPin, kBacklightFreq, kBacklightResolution);
  ledcWrite(kBacklightPin, 0);
  g_backlight_enabled = false;
  g_backlight_enable_at_ms = 0;
}

void init_sensors() {
  g_touch_i2c.begin(kTouchI2cSda, kTouchI2cScl, 400000U);
  g_sensor_i2c.begin(kSensorI2cSda, kSensorI2cScl, 100000U);
  g_aht_ready = g_aht.begin(&g_sensor_i2c);

  // Read GT911 configured resolution for coordinate scaling
  uint8_t res[4] = {0};
  if (gt911_read(0x8048, res, 4)) {
    const uint16_t x_max = static_cast<uint16_t>(res[1] << 8 | res[0]);
    const uint16_t y_max = static_cast<uint16_t>(res[3] << 8 | res[2]);
    if (x_max > 0) g_touch_x_max = x_max;
    if (y_max > 0) g_touch_y_max = y_max;
    Serial.printf("[touch] GT911 resolution: %ux%u\n", g_touch_x_max, g_touch_y_max);
  }
}

void init_network() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(wifi_event_handler);
  WiFi.setAutoReconnect(true);
  g_mqtt.setBufferSize(1024);
  g_mqtt.setServer(g_cfg_mqtt_host.c_str(), g_cfg_mqtt_port);
  g_mqtt.setCallback(mqtt_on_message);
  configTzTime(THERMOSTAT_TIME_TZ, THERMOSTAT_TIME_NTP_1, THERMOSTAT_TIME_NTP_2,
               THERMOSTAT_TIME_NTP_3);
}

void poll_sensors(uint32_t now_ms) {
  if (g_runtime == nullptr) return;
  if ((now_ms - g_last_sensor_poll_ms) < kSensorPollMs) return;

  g_last_sensor_poll_ms = now_ms;

  float t = 21.5f;
  float h = 45.0f;

  if (g_aht_ready) {
    sensors_event_t humidity, temp;
    if (g_aht.getEvent(&humidity, &temp)) {
      t = temp.temperature;
      h = humidity.relative_humidity;
    }
  }

  g_runtime->on_local_sensor_update(t, h);
  if (g_have_weather_data) {
    g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_icon);
  }
}

}  // namespace

void thermostat_firmware_setup() {
  g_cfg_ready = g_cfg.begin("cfg_disp", false);
  load_runtime_config();
  g_boot_count = g_cfg_ready ? (g_cfg.getUInt("boot_cnt", 0U) + 1U) : 0U;
  if (g_cfg_ready) {
    g_cfg.putUInt("boot_cnt", g_boot_count);
  }
  g_reset_reason = reset_reason_text(esp_reset_reason());

  ThermostatDeviceRuntimeConfig cfg;
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(cfg.transport.peer_mac, kBroadcast, sizeof(kBroadcast));
  cfg.transport.channel = g_cfg_espnow_channel;
  cfg.transport.heartbeat_interval_ms = 10000;
  cfg.controller_connection_timeout_ms = g_cfg_controller_timeout_ms;
  parse_mac(g_cfg_espnow_peer_mac.c_str(), cfg.transport.peer_mac);
  uint8_t lmk[16] = {0};
  if (parse_lmk_hex(g_cfg_espnow_lmk.c_str(), lmk) &&
      !is_broadcast_mac(cfg.transport.peer_mac)) {
    memcpy(cfg.transport.lmk, lmk, sizeof(lmk));
    cfg.transport.encrypted = true;
  }

  static ThermostatDeviceRuntime runtime(cfg);
  g_runtime = &runtime;

  g_screen.set_display_timeout_ms(g_display_timeout_ms);
  g_screen.on_boot(millis());

  init_backlight();
  init_sensors();
  init_network();
  init_display_and_lvgl();
  create_ui();
  g_backlight_enable_at_ms = millis() + kBacklightEnableDelayMs;

  const bool runtime_ok = g_runtime->begin();
  g_last_espnow_error = runtime_ok ? "none" : "begin_failed";
  g_runtime->set_local_temperature_compensation_c(g_cfg_temp_comp_c);
  g_runtime->set_temperature_unit(g_cfg_temp_unit_f ? TemperatureUnit::Fahrenheit
                                                    : TemperatureUnit::Celsius);
  ota_rollback_begin();
}

void thermostat_firmware_loop() {
  uint32_t now = millis();
  if (g_reboot_requested && static_cast<int32_t>(now - g_reboot_at_ms) >= 0) {
    shutdown_display_for_reboot();
    delay(kRebootPanelOffDelayMs);
    ESP.restart();
    return;
  }

  if (!g_backlight_enabled && g_backlight_enable_at_ms > 0 &&
      static_cast<int32_t>(now - g_backlight_enable_at_ms) >= 0) {
    g_backlight_enabled = true;
    apply_backlight(g_screen.screensaver_active());
  }

  if ((now - g_last_ui_tick_ms) >= kUiTickMs) {
    lv_tick_inc(now - g_last_ui_tick_ms);
    g_last_ui_tick_ms = now;
    lv_timer_handler();
  }

  // Re-capture time after LVGL processing so the runtime tick uses a
  // timestamp >= any millis() call made inside touch_read_cb.  Without
  // this, on_user_interaction(millis()) can set last_interaction_ms to a
  // value slightly *after* `now`, causing an unsigned underflow in
  // g_screen.tick(now) that immediately triggers the screensaver.
  now = millis();

  if ((now - g_last_runtime_tick_ms) >= kRuntimeTickMs) {
    g_last_runtime_tick_ms = now;
    if (g_runtime != nullptr) {
      g_runtime->tick(now);
    }
    g_screen.tick(now);
    show_page(g_screen.current_page());
    apply_backlight(g_screen.screensaver_active());
  }

  ensure_wifi_connected(now);
  ensure_mdns_ready();
  ensure_web_ready();
  ensure_ota_ready();
  ensure_mqtt_connected(now);
  g_mqtt.loop();
  poll_weather(now);
  ota_rollback_check(WiFi.status() == WL_CONNECTED && g_mqtt.connected());

  // Apply accumulated MQTT controller state to app layer
  if (g_runtime != nullptr && g_mqtt_ctrl_last_update_ms > g_mqtt_ctrl_last_applied_ms) {
    g_runtime->on_controller_state_update(
        now, g_mqtt_ctrl_state, g_mqtt_ctrl_lockout,
        g_mqtt_ctrl_mode, g_mqtt_ctrl_fan,
        g_mqtt_ctrl_setpoint_c, g_mqtt_ctrl_filter_runtime_s);
    g_mqtt_ctrl_last_applied_ms = g_mqtt_ctrl_last_update_ms;
  }

  if (g_mqtt.connected() && (now - g_last_mqtt_publish_ms) >= kMqttPublishMs) {
    g_last_mqtt_publish_ms = now;
    mqtt_publish_state();
  }

  poll_sensors(now);

  if ((now - g_last_ui_refresh_ms) >= kUiRefreshMs) {
    g_last_ui_refresh_ms = now;
    refresh_ui();
  }
}

}  // namespace thermostat

#endif
