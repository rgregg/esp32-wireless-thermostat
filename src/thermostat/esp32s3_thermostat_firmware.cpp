#if defined(ARDUINO) && defined(THERMOSTAT_ROLE_THERMOSTAT)

#include "thermostat/esp32s3_thermostat_firmware.h"

#include <cctype>
#include <cstring>
#include <ctime>
#include <cstdio>

#include <Wire.h>
#include <lvgl.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <esp_system.h>

#include "thermostat/thermostat_device_runtime.h"
#include "thermostat/thermostat_screen_controller.h"

#include <Adafruit_AHTX0.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

namespace thermostat {
namespace {

constexpr int kDisplayWidth = 800;
constexpr int kDisplayHeight = 480;
constexpr uint32_t kUiTickMs = 5;
constexpr uint32_t kRuntimeTickMs = 200;
constexpr uint32_t kSensorPollMs = 60000;
constexpr uint32_t kUiRefreshMs = 500;

constexpr int kBacklightPin = 2;
constexpr int kBacklightChannel = 0;
constexpr int kBacklightFreq = 800;
constexpr int kBacklightResolution = 8;

constexpr uint8_t kGt911Addr = 0x5D;

constexpr int kTouchI2cSda = 19;
constexpr int kTouchI2cScl = 20;
constexpr int kSensorI2cSda = 18;
constexpr int kSensorI2cScl = 17;
constexpr uint32_t kNetworkRetryMs = 5000;
constexpr uint32_t kProvisionStartDelayMs = 15000;
constexpr uint32_t kMqttPublishMs = 10000;

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

#ifndef THERMOSTAT_MQTT_OUTDOOR_TEMP_TOPIC
#define THERMOSTAT_MQTT_OUTDOOR_TEMP_TOPIC ""
#endif

#ifndef THERMOSTAT_MQTT_WEATHER_CONDITION_TOPIC
#define THERMOSTAT_MQTT_WEATHER_CONDITION_TOPIC ""
#endif

#ifndef THERMOSTAT_PROV_POP
#define THERMOSTAT_PROV_POP "thermostat-setup"
#endif

#ifndef THERMOSTAT_PROV_SERVICE_NAME
#define THERMOSTAT_PROV_SERVICE_NAME "PROV_ESP32_THERMOSTAT"
#endif

#ifndef THERMOSTAT_PROV_RESET_PROVISIONED
#define THERMOSTAT_PROV_RESET_PROVISIONED 0
#endif

#ifndef THERMOSTAT_DISPLAY_TIMEOUT_S
#define THERMOSTAT_DISPLAY_TIMEOUT_S 300
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

esp_lcd_panel_handle_t g_panel = nullptr;

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
lv_obj_t *g_screen_time_label = nullptr;
lv_obj_t *g_settings_diag_label = nullptr;

lv_obj_t *g_setpoint_column = nullptr;

uint32_t g_last_ui_tick_ms = 0;
uint32_t g_last_runtime_tick_ms = 0;
uint32_t g_last_sensor_poll_ms = 0;
uint32_t g_last_ui_refresh_ms = 0;
uint32_t g_last_wifi_attempt_ms = 0;
uint32_t g_first_wifi_attempt_ms = 0;
uint32_t g_last_mqtt_attempt_ms = 0;
uint32_t g_last_mqtt_publish_ms = 0;
bool g_wifi_has_attempted_stored_connect = false;
bool g_wifi_provisioning_started = false;
float g_outdoor_temp_c = 6.0f;
std::string g_weather_condition = "Cloudy";
bool g_have_outdoor_temp = false;
bool g_have_weather_condition = false;
uint32_t g_display_timeout_ms = static_cast<uint32_t>(THERMOSTAT_DISPLAY_TIMEOUT_S) * 1000UL;
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
String g_cfg_mqtt_outdoor_temp_topic = THERMOSTAT_MQTT_OUTDOOR_TEMP_TOPIC;
String g_cfg_mqtt_weather_condition_topic = THERMOSTAT_MQTT_WEATHER_CONDITION_TOPIC;
String g_cfg_ota_hostname = THERMOSTAT_OTA_HOSTNAME;
String g_cfg_ota_password = THERMOSTAT_OTA_PASSWORD;
uint8_t g_cfg_espnow_channel = THERMOSTAT_ESPNOW_CHANNEL;
String g_cfg_espnow_peer_mac = THERMOSTAT_ESPNOW_PEER_MAC;
String g_cfg_espnow_lmk = THERMOSTAT_ESPNOW_LMK;
uint32_t g_cfg_controller_timeout_ms = 30000;
bool g_cfg_wifi_reconnect_required = false;
bool g_cfg_mqtt_reconfigure_required = false;
bool g_cfg_reboot_required = false;
uint16_t *g_screenshot_fb = nullptr;
uint32_t g_boot_count = 0;
String g_reset_reason = "unknown";

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
  g_cfg_mqtt_outdoor_temp_topic = g_cfg.getString("outdoor_t", g_cfg_mqtt_outdoor_temp_topic);
  g_cfg_mqtt_weather_condition_topic =
      g_cfg.getString("weather_t", g_cfg_mqtt_weather_condition_topic);
  g_cfg_ota_hostname = g_cfg.getString("ota_host", g_cfg_ota_hostname);
  g_cfg_ota_password = g_cfg.getString("ota_pwd", g_cfg_ota_password);
  g_cfg_espnow_channel = static_cast<uint8_t>(g_cfg.getUChar("esp_ch", g_cfg_espnow_channel));
  g_cfg_espnow_peer_mac = g_cfg.getString("esp_peer", g_cfg_espnow_peer_mac);
  g_cfg_espnow_lmk = g_cfg.getString("esp_lmk", g_cfg_espnow_lmk);
  g_cfg_controller_timeout_ms = g_cfg.getUInt("ctrl_to", g_cfg_controller_timeout_ms);
  const uint32_t display_timeout_s = g_cfg.getUInt("disp_to_s", THERMOSTAT_DISPLAY_TIMEOUT_S);
  g_display_timeout_ms = display_timeout_s * 1000UL;
  g_cfg_temp_comp_c = g_cfg.getFloat("temp_comp", 0.0f);
  g_cfg_temp_unit_f = g_cfg.getBool("temp_u_f", false);
}

String topic_for(const char *suffix) {
  String out(g_cfg_mqtt_base_topic);
  out += "/";
  out += suffix;
  return out;
}

void publish_cfg_value(const char *key, const String &value, bool redact) {
  if (!g_mqtt.connected()) return;
  String topic = topic_for("cfg");
  topic += "/";
  topic += key;
  topic += "/state";
  const char *payload = value.c_str();
  if (redact) {
    payload = value.length() > 0 ? "set" : "unset";
  }
  g_mqtt.publish(topic.c_str(), payload, true);
}

void publish_all_cfg_state() {
  publish_cfg_value("wifi_ssid", g_cfg_wifi_ssid, false);
  publish_cfg_value("wifi_password", g_cfg_wifi_password, true);
  publish_cfg_value("mqtt_host", g_cfg_mqtt_host, false);
  publish_cfg_value("mqtt_port", String(g_cfg_mqtt_port), false);
  publish_cfg_value("mqtt_user", g_cfg_mqtt_user, false);
  publish_cfg_value("mqtt_password", g_cfg_mqtt_password, true);
  publish_cfg_value("mqtt_client_id", g_cfg_mqtt_client_id, false);
  publish_cfg_value("mqtt_base_topic", g_cfg_mqtt_base_topic, false);
  publish_cfg_value("discovery_prefix", g_cfg_discovery_prefix, false);
  publish_cfg_value("shared_device_id", g_cfg_shared_device_id, false);
  publish_cfg_value("outdoor_temp_topic", g_cfg_mqtt_outdoor_temp_topic, false);
  publish_cfg_value("weather_condition_topic", g_cfg_mqtt_weather_condition_topic, false);
  publish_cfg_value("display_timeout_s", String(g_display_timeout_ms / 1000UL), false);
  publish_cfg_value("temp_comp_c", String(g_cfg_temp_comp_c, 2), false);
  publish_cfg_value("temperature_unit", g_cfg_temp_unit_f ? "f" : "c", false);
  publish_cfg_value("ota_hostname", g_cfg_ota_hostname, false);
  publish_cfg_value("ota_password", g_cfg_ota_password, true);
  publish_cfg_value("espnow_channel", String(g_cfg_espnow_channel), false);
  publish_cfg_value("espnow_peer_mac", g_cfg_espnow_peer_mac, false);
  publish_cfg_value("espnow_lmk", g_cfg_espnow_lmk, true);
  publish_cfg_value("controller_timeout_ms", String(g_cfg_controller_timeout_ms), false);
  publish_cfg_value("reboot_required", g_cfg_reboot_required ? "1" : "0", false);
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
  } else if (key == "outdoor_temp_topic") {
    g_cfg_mqtt_outdoor_temp_topic = value;
    g_cfg.putString("outdoor_t", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "weather_condition_topic") {
    g_cfg_mqtt_weather_condition_topic = value;
    g_cfg.putString("weather_t", value);
    g_cfg_mqtt_reconfigure_required = true;
  } else if (key == "display_timeout_s") {
    long seconds = atol(raw_value);
    if (seconds < 30) seconds = 30;
    if (seconds > 600) seconds = 600;
    g_display_timeout_ms = static_cast<uint32_t>(seconds) * 1000UL;
    g_cfg.putUInt("disp_to_s", static_cast<uint32_t>(seconds));
    g_screen.set_display_timeout_ms(g_display_timeout_ms);
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
  if (g_mqtt.connected()) {
    publish_cfg_value(key.c_str(), value,
                      key == "wifi_password" || key == "mqtt_password" ||
                          key == "ota_password" || key == "espnow_lmk");
  }
  return true;
}

bool parse_bool_payload(const char *value) {
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
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
  body += "\"outdoor_temp_topic\":\"" + json_escape(g_cfg_mqtt_outdoor_temp_topic) + "\",";
  body += "\"weather_condition_topic\":\"" + json_escape(g_cfg_mqtt_weather_condition_topic) + "\",";
  body += "\"display_timeout_s\":" + String(g_display_timeout_ms / 1000UL) + ",";
  body += "\"temp_comp_c\":" + String(g_cfg_temp_comp_c, 2) + ",";
  body += "\"temperature_unit\":\"" + String(g_cfg_temp_unit_f ? "f" : "c") + "\",";
  body += "\"ota_hostname\":\"" + json_escape(g_cfg_ota_hostname) + "\",";
  body += "\"ota_password\":\"" + String(g_cfg_ota_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"espnow_channel\":" + String(g_cfg_espnow_channel) + ",";
  body += "\"espnow_peer_mac\":\"" + json_escape(g_cfg_espnow_peer_mac) + "\",";
  body += "\"espnow_lmk\":\"" + String(g_cfg_espnow_lmk.length() > 0 ? "set" : "unset") + "\",";
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
  if (g_screenshot_fb == nullptr) {
    g_web.send(503, "text/plain", "screenshot buffer unavailable");
    return;
  }
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
      const uint16_t px = g_screenshot_fb[(y * kDisplayWidth) + x];
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
  html.reserve(4096);
  html += "<html><body><h1>Thermostat Display Config</h1>";
  html += "<p><a href=\"/config\">JSON config</a> | <a href=\"/screenshot\">Screenshot</a></p>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"wifi_ssid\" value=\"" + g_cfg_wifi_ssid + "\"><br>";
  html += "wifi_password: <input name=\"wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"mqtt_host\" value=\"" + g_cfg_mqtt_host + "\"><br>";
  html += "mqtt_port: <input name=\"mqtt_port\" value=\"" + String(g_cfg_mqtt_port) + "\"><br>";
  html += "mqtt_user: <input name=\"mqtt_user\" value=\"" + g_cfg_mqtt_user + "\"><br>";
  html += "mqtt_password: <input name=\"mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"mqtt_client_id\" value=\"" + g_cfg_mqtt_client_id + "\"><br>";
  html += "mqtt_base_topic: <input name=\"mqtt_base_topic\" value=\"" + g_cfg_mqtt_base_topic + "\"><br>";
  html += "discovery_prefix: <input name=\"discovery_prefix\" value=\"" + g_cfg_discovery_prefix + "\"><br>";
  html += "shared_device_id: <input name=\"shared_device_id\" value=\"" + g_cfg_shared_device_id + "\"><br>";
  html += "display_timeout_s: <input name=\"display_timeout_s\" value=\"" + String(g_display_timeout_ms / 1000UL) + "\"><br>";
  html += "temp_comp_c: <input name=\"temp_comp_c\" value=\"" + String(g_cfg_temp_comp_c, 2) + "\"><br>";
  html += "temperature_unit (c/f): <input name=\"temperature_unit\" value=\"" + String(g_cfg_temp_unit_f ? "f" : "c") + "\"><br>";
  html += "outdoor_temp_topic: <input name=\"outdoor_temp_topic\" value=\"" + g_cfg_mqtt_outdoor_temp_topic + "\"><br>";
  html += "weather_condition_topic: <input name=\"weather_condition_topic\" value=\"" + g_cfg_mqtt_weather_condition_topic + "\"><br>";
  html += "ota_hostname: <input name=\"ota_hostname\" value=\"" + g_cfg_ota_hostname + "\"><br>";
  html += "ota_password: <input name=\"ota_password\" value=\"\"><br>";
  html += "espnow_channel: <input name=\"espnow_channel\" value=\"" + String(g_cfg_espnow_channel) + "\"><br>";
  html += "espnow_peer_mac: <input name=\"espnow_peer_mac\" value=\"" + g_cfg_espnow_peer_mac + "\"><br>";
  html += "espnow_lmk: <input name=\"espnow_lmk\" value=\"\"><br>";
  html += "controller_timeout_ms: <input name=\"controller_timeout_ms\" value=\"" + String(g_cfg_controller_timeout_ms) + "\"><br>";
  html += "<button type=\"submit\">Save</button></form>";
  html += "<p>reboot_required=" + String(g_cfg_reboot_required ? "true" : "false") + "</p>";
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
    g_web.on("/screenshot", HTTP_GET, web_handle_screenshot);
    g_web.begin();
    g_web_started = true;
  }
  g_web.handleClient();
}

std::string current_time_text() {
  time_t now = time(nullptr);
  if (now <= 0) {
    return "--:--";
  }
  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%I:%M %p", &local_tm);
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

  snprintf(buf, sizeof(buf), "%.2f", g_runtime->local_temperature_compensation_c());
  g_mqtt.publish(topic_for("state/temp_comp_c").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_display_timeout_ms / 1000UL));
  g_mqtt.publish(topic_for("state/display_timeout_s").c_str(), buf, true);
  g_mqtt.publish(topic_for("state/firmware_version").c_str(), THERMOSTAT_FIRMWARE_VERSION, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_boot_count));
  g_mqtt.publish(topic_for("state/boot_count").c_str(), buf, true);
  g_mqtt.publish(topic_for("state/reset_reason").c_str(), g_reset_reason.c_str(), true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(millis() / 1000UL));
  g_mqtt.publish(topic_for("state/uptime_s").c_str(), buf, true);

  if (WiFi.status() == WL_CONNECTED) {
    g_mqtt.publish(topic_for("state/wifi_ip").c_str(), WiFi.localIP().toString().c_str(), true);
    g_mqtt.publish(topic_for("state/wifi_mac").c_str(), WiFi.macAddress().c_str(), true);
    g_mqtt.publish(topic_for("state/wifi_ssid").c_str(), WiFi.SSID().c_str(), true);
    snprintf(buf, sizeof(buf), "%d", WiFi.channel());
    g_mqtt.publish(topic_for("state/wifi_channel").c_str(), buf, true);
    snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
    g_mqtt.publish(topic_for("state/wifi_rssi").c_str(), buf, true);
  }

  bool ok = false;
  const float indoor_c = parse_numeric_prefix(g_runtime->indoor_temp_text(), &ok);
  if (ok) {
    snprintf(buf, sizeof(buf), "%.2f", indoor_c);
    g_mqtt.publish(topic_for("state/current_temp_c").c_str(), buf, true);
  }

  ok = false;
  const float indoor_h = parse_numeric_prefix(g_runtime->indoor_humidity_text(), &ok);
  if (ok) {
    snprintf(buf, sizeof(buf), "%.2f", indoor_h);
    g_mqtt.publish(topic_for("state/current_humidity").c_str(), buf, true);
  }

  g_mqtt.publish(topic_for("state/status").c_str(), g_runtime->status_text(now).c_str(), true);
}

void mqtt_publish_discovery() {
  if (!g_mqtt.connected() || g_mqtt_discovery_sent) return;

  const String base = g_cfg_mqtt_base_topic;
  const String node = g_cfg_shared_device_id;
  const String config_topic = g_cfg_discovery_prefix + "/climate/" + node +
                              "/config";

  char payload[1500];
  snprintf(
      payload, sizeof(payload),
      "{\"name\":\"Furnace Thermostat\",\"uniq_id\":\"%s_climate\",\"mode_cmd_t\":\"%s/cmd/"
      "mode\",\"mode_stat_t\":\"%s/state/mode\",\"temp_cmd_t\":\"%s/cmd/target_temp_c\","
      "\"temp_stat_t\":\"%s/state/target_temp_c\",\"curr_temp_t\":\"%s/state/current_temp_c\","
      "\"fan_mode_cmd_t\":\"%s/cmd/fan_mode\",\"fan_mode_stat_t\":\"%s/state/fan_mode\","
      "\"curr_hum_t\":\"%s/state/current_humidity\",\"modes\":[\"off\",\"heat\",\"cool\"],"
      "\"fan_modes\":[\"auto\",\"on\",\"circulate\"],\"avty_t\":\"%s/state/availability\","
      "\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\",\"min_temp\":5,\"max_temp\":35,"
      "\"temp_step\":0.5,\"temp_unit\":\"C\",\"dev\":{\"ids\":[\"%s\"],"
      "\"name\":\"Wireless Thermostat System\",\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
      node.c_str(), base.c_str(), base.c_str(), base.c_str(), base.c_str(), base.c_str(),
      base.c_str(), base.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(config_topic.c_str(), payload, true);

  const String timeout_config = g_cfg_discovery_prefix + "/number/" + node +
                                "_display_timeout/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Display Timeout\",\"uniq_id\":\"%s_display_timeout\","
           "\"cmd_t\":\"%s/cmd/display_timeout_s\",\"stat_t\":\"%s/state/display_timeout_s\","
           "\"min\":30,\"max\":600,\"step\":5,\"mode\":\"box\",\"unit_of_meas\":\"s\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(timeout_config.c_str(), payload, true);

  const String fw_config = g_cfg_discovery_prefix + "/sensor/" + node + "_firmware/config";
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Firmware Version\",\"uniq_id\":\"%s_firmware\",\"stat_t\":\"%s/state/firmware_version\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           node.c_str(), base.c_str(), node.c_str());
  g_mqtt.publish(fw_config.c_str(), payload, true);

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
    const float celsius = static_cast<float>(atof(value));
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
  } else if (topic_str == topic_for("cmd/filter_reset")) {
    if (parse_bool_payload(normalized)) {
      g_runtime->request_filter_reset(now);
    }
  } else if (topic_str == topic_for("cmd/temp_comp_c")) {
    g_cfg_temp_comp_c = static_cast<float>(atof(value));
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
  } else if (g_cfg_mqtt_outdoor_temp_topic.length() > 0 &&
             topic_str == g_cfg_mqtt_outdoor_temp_topic) {
    g_outdoor_temp_c = static_cast<float>(atof(value));
    g_have_outdoor_temp = true;
    g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_condition);
  } else if (g_cfg_mqtt_weather_condition_topic.length() > 0 &&
             topic_str == g_cfg_mqtt_weather_condition_topic) {
    g_weather_condition = value;
    g_have_weather_condition = true;
    g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_condition);
  }

  mqtt_publish_state();
}

String improv_service_name() {
  const String configured(THERMOSTAT_PROV_SERVICE_NAME);
  if (configured.length() > 0) return configured;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  if (mac.length() >= 6) {
    return String("PROV_") + mac.substring(mac.length() - 6);
  }
  return "PROV_ESP32_THERMOSTAT";
}

void start_wifi_provisioning() {
  if (g_wifi_provisioning_started) return;
  const String service_name = improv_service_name();
  const bool reset_provisioned = THERMOSTAT_PROV_RESET_PROVISIONED != 0;
#if CONFIG_BLUEDROID_ENABLED
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
                          WIFI_PROV_SECURITY_1, THERMOSTAT_PROV_POP, service_name.c_str(), nullptr,
                          nullptr, reset_provisioned);
  WiFiProv.printQR(service_name.c_str(), THERMOSTAT_PROV_POP, "ble");
#else
  WiFiProv.beginProvision(WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SCHEME_HANDLER_NONE,
                          WIFI_PROV_SECURITY_1, THERMOSTAT_PROV_POP, service_name.c_str(), nullptr,
                          nullptr, reset_provisioned);
  WiFiProv.printQR(service_name.c_str(), THERMOSTAT_PROV_POP, "softap");
#endif
  g_wifi_provisioning_started = true;
}

void wifi_event_handler(arduino_event_t *event) {
  if (event == nullptr) return;
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_wifi_provisioning_started = false;
      break;
    case ARDUINO_EVENT_PROV_START:
      g_wifi_provisioning_started = true;
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

  if (!ok) return;

  g_mqtt.subscribe(topic_for("cmd/mode").c_str());
  g_mqtt.subscribe(topic_for("cmd/fan_mode").c_str());
  g_mqtt.subscribe(topic_for("cmd/target_temp_c").c_str());
  g_mqtt.subscribe(topic_for("cmd/unit").c_str());
  g_mqtt.subscribe(topic_for("cmd/sync").c_str());
  g_mqtt.subscribe(topic_for("cmd/filter_reset").c_str());
  g_mqtt.subscribe(topic_for("cmd/temp_comp_c").c_str());
  g_mqtt.subscribe(topic_for("cmd/display_timeout_s").c_str());
  g_mqtt.subscribe(topic_for("cfg/+/set").c_str());
  if (g_cfg_mqtt_outdoor_temp_topic.length() > 0) {
    g_mqtt.subscribe(g_cfg_mqtt_outdoor_temp_topic.c_str());
  }
  if (g_cfg_mqtt_weather_condition_topic.length() > 0) {
    g_mqtt.subscribe(g_cfg_mqtt_weather_condition_topic.c_str());
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
  if (g_screenshot_fb != nullptr && area != nullptr && color_p != nullptr) {
    const int width = (area->x2 - area->x1 + 1);
    const int height = (area->y2 - area->y1 + 1);
    for (int y = 0; y < height; ++y) {
      const int dst_y = area->y1 + y;
      if (dst_y < 0 || dst_y >= kDisplayHeight) continue;
      const int dst_x = area->x1;
      if (dst_x < 0 || dst_x >= kDisplayWidth) continue;
      const int copy_w = (dst_x + width <= kDisplayWidth) ? width : (kDisplayWidth - dst_x);
      memcpy(&g_screenshot_fb[(dst_y * kDisplayWidth) + dst_x],
             &color_p[y * width], static_cast<size_t>(copy_w) * sizeof(uint16_t));
    }
  }
  if (g_panel != nullptr) {
    esp_lcd_panel_draw_bitmap(g_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                              color_p);
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

void poll_touch() {
  uint8_t status = 0;
  if (!gt911_read(0x814E, &status, 1)) {
    g_touch.touched = false;
    return;
  }

  const uint8_t points = status & 0x0F;
  if ((status & 0x80) == 0 || points == 0) {
    g_touch.touched = false;
    return;
  }

  uint8_t data[8] = {0};
  if (!gt911_read(0x8150, data, sizeof(data))) {
    g_touch.touched = false;
    return;
  }

  const uint16_t x = static_cast<uint16_t>(data[1] << 8 | data[0]);
  const uint16_t y = static_cast<uint16_t>(data[3] << 8 | data[2]);

  g_touch.touched = true;
  g_touch.x = static_cast<int16_t>(x);
  g_touch.y = static_cast<int16_t>(y);

  gt911_write(0x814E, 0);
}

void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
  poll_touch();
  if (g_touch.touched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = g_touch.x;
    data->point.y = g_touch.y;
    g_screen.on_user_interaction(millis());
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
}

void apply_backlight(bool screensaver_active) {
  const uint8_t duty = screensaver_active ? 40 : 255;
  ledcWrite(kBacklightChannel, duty);
}

void refresh_ui() {
  if (g_runtime == nullptr || g_status_label == nullptr) return;

  const uint32_t now = millis();

  lv_label_set_text(g_status_label, g_runtime->status_text(now).c_str());
  lv_label_set_text(g_indoor_label, g_runtime->indoor_temp_text().c_str());
  lv_label_set_text(g_humidity_label, g_runtime->indoor_humidity_text().c_str());
  lv_label_set_text(g_setpoint_label, g_runtime->setpoint_text().c_str());
  lv_label_set_text(g_weather_label, g_runtime->weather_text().c_str());

  lv_label_set_text(g_screen_time_label, current_time_text().c_str());
  if (g_settings_diag_label != nullptr) {
    char diag[256];
    String ip_str = WiFi.isConnected() ? WiFi.localIP().toString() : String("N/A");
    String mac_str = WiFi.macAddress();
    String ssid_str = WiFi.isConnected() ? WiFi.SSID() : String("N/A");
    const char *ssid = WiFi.isConnected() ? WiFi.SSID().c_str() : "N/A";
    const char *ip = ip_str.c_str();
    const char *mac = mac_str.c_str();
    ssid = ssid_str.c_str();
    snprintf(diag, sizeof(diag),
             "IP: %s\nMAC: %s\nSSID: %s\nCH: %d RSSI: %d\nFW: %s",
             ip, mac, ssid, WiFi.channel(), WiFi.RSSI(), THERMOSTAT_FIRMWARE_VERSION);
    lv_label_set_text(g_settings_diag_label, diag);
  }

  g_screen.on_mode_changed(g_runtime->local_mode());
  if (g_setpoint_column != nullptr) {
    if (g_screen.setpoint_visible()) {
      lv_obj_clear_flag(g_setpoint_column, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_setpoint_column, LV_OBJ_FLAG_HIDDEN);
    }
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
  if (g_runtime == nullptr) return;
  auto unit = static_cast<TemperatureUnit>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_runtime->set_temperature_unit(unit);
}

void btn_sync_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  g_runtime->request_sync(millis());
}

void btn_filter_reset_cb(lv_event_t *) {
  if (g_runtime == nullptr) return;
  g_runtime->request_filter_reset(millis());
}

lv_obj_t *make_page() {
  lv_obj_t *p = lv_obj_create(lv_scr_act());
  lv_obj_set_size(p, kDisplayWidth, kDisplayHeight - 50);
  lv_obj_set_pos(p, 0, 50);
  return p;
}

void create_ui() {
  g_tabs = lv_btnmatrix_create(lv_scr_act());
  static const char *tab_map[] = {"HOME", "FAN", "MODE", "SETTINGS", ""};
  lv_btnmatrix_set_map(g_tabs, tab_map);
  lv_obj_set_size(g_tabs, kDisplayWidth, 50);
  lv_obj_add_event_cb(g_tabs, tab_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

  g_home_page = make_page();
  g_fan_page = make_page();
  g_mode_page = make_page();
  g_settings_page = make_page();
  g_screensaver_page = make_page();

  g_status_label = lv_label_create(g_home_page);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 20, 20);

  g_indoor_label = lv_label_create(g_home_page);
  lv_obj_align(g_indoor_label, LV_ALIGN_TOP_LEFT, 20, 60);

  g_humidity_label = lv_label_create(g_home_page);
  lv_obj_align(g_humidity_label, LV_ALIGN_TOP_LEFT, 20, 100);

  g_weather_label = lv_label_create(g_home_page);
  lv_obj_align(g_weather_label, LV_ALIGN_TOP_LEFT, 20, 140);

  g_setpoint_column = lv_obj_create(g_home_page);
  lv_obj_set_size(g_setpoint_column, 120, 240);
  lv_obj_align(g_setpoint_column, LV_ALIGN_TOP_RIGHT, -20, 20);

  lv_obj_t *btn_up = lv_btn_create(g_setpoint_column);
  lv_obj_set_size(btn_up, 90, 50);
  lv_obj_align(btn_up, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_add_event_cb(btn_up, btn_setpoint_up_cb, LV_EVENT_CLICKED, nullptr);
  lv_label_set_text(lv_label_create(btn_up), "+");

  g_setpoint_label = lv_label_create(g_setpoint_column);
  lv_obj_align(g_setpoint_label, LV_ALIGN_TOP_MID, 0, 80);

  lv_obj_t *btn_down = lv_btn_create(g_setpoint_column);
  lv_obj_set_size(btn_down, 90, 50);
  lv_obj_align(btn_down, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_add_event_cb(btn_down, btn_setpoint_down_cb, LV_EVENT_CLICKED, nullptr);
  lv_label_set_text(lv_label_create(btn_down), "-");

  lv_obj_t *fan_auto = lv_btn_create(g_fan_page);
  lv_obj_set_size(fan_auto, 220, 60);
  lv_obj_align(fan_auto, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_add_event_cb(fan_auto, btn_fan_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::Automatic)));
  lv_label_set_text(lv_label_create(fan_auto), "Automatic");

  lv_obj_t *fan_on = lv_btn_create(g_fan_page);
  lv_obj_set_size(fan_on, 220, 60);
  lv_obj_align(fan_on, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_add_event_cb(fan_on, btn_fan_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::AlwaysOn)));
  lv_label_set_text(lv_label_create(fan_on), "Always On");

  lv_obj_t *fan_circ = lv_btn_create(g_fan_page);
  lv_obj_set_size(fan_circ, 220, 60);
  lv_obj_align(fan_circ, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_add_event_cb(fan_circ, btn_fan_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FanMode::Circulate)));
  lv_label_set_text(lv_label_create(fan_circ), "Circulate");

  lv_obj_t *mode_heat = lv_btn_create(g_mode_page);
  lv_obj_set_size(mode_heat, 220, 60);
  lv_obj_align(mode_heat, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_add_event_cb(mode_heat, btn_mode_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Heat)));
  lv_label_set_text(lv_label_create(mode_heat), "Heat");

  lv_obj_t *mode_cool = lv_btn_create(g_mode_page);
  lv_obj_set_size(mode_cool, 220, 60);
  lv_obj_align(mode_cool, LV_ALIGN_TOP_MID, 0, 100);
  lv_obj_add_event_cb(mode_cool, btn_mode_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Cool)));
  lv_label_set_text(lv_label_create(mode_cool), "Cool");

  lv_obj_t *mode_off = lv_btn_create(g_mode_page);
  lv_obj_set_size(mode_off, 220, 60);
  lv_obj_align(mode_off, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_add_event_cb(mode_off, btn_mode_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(FurnaceMode::Off)));
  lv_label_set_text(lv_label_create(mode_off), "Off");

  lv_obj_t *unit_f = lv_btn_create(g_settings_page);
  lv_obj_set_size(unit_f, 180, 50);
  lv_obj_align(unit_f, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(unit_f, btn_unit_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(TemperatureUnit::Fahrenheit)));
  lv_label_set_text(lv_label_create(unit_f), "Fahrenheit");

  lv_obj_t *unit_c = lv_btn_create(g_settings_page);
  lv_obj_set_size(unit_c, 180, 50);
  lv_obj_align(unit_c, LV_ALIGN_TOP_LEFT, 220, 20);
  lv_obj_add_event_cb(unit_c, btn_unit_cb, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<uintptr_t>(TemperatureUnit::Celsius)));
  lv_label_set_text(lv_label_create(unit_c), "Celsius");

  lv_obj_t *sync_btn = lv_btn_create(g_settings_page);
  lv_obj_set_size(sync_btn, 180, 50);
  lv_obj_align(sync_btn, LV_ALIGN_TOP_LEFT, 20, 100);
  lv_obj_add_event_cb(sync_btn, btn_sync_cb, LV_EVENT_CLICKED, nullptr);
  lv_label_set_text(lv_label_create(sync_btn), "Sync");

  lv_obj_t *filter_btn = lv_btn_create(g_settings_page);
  lv_obj_set_size(filter_btn, 180, 50);
  lv_obj_align(filter_btn, LV_ALIGN_TOP_LEFT, 220, 100);
  lv_obj_add_event_cb(filter_btn, btn_filter_reset_cb, LV_EVENT_CLICKED, nullptr);
  lv_label_set_text(lv_label_create(filter_btn), "Filter Reset");

  g_settings_diag_label = lv_label_create(g_settings_page);
  lv_obj_set_width(g_settings_diag_label, 760);
  lv_obj_align(g_settings_diag_label, LV_ALIGN_TOP_LEFT, 20, 180);
  lv_label_set_long_mode(g_settings_diag_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_settings_diag_label, "Diagnostics...");

  g_screen_time_label = lv_label_create(g_screensaver_page);
  lv_obj_align(g_screen_time_label, LV_ALIGN_CENTER, 0, 0);

  show_page(ThermostatPage::Home);
}

void init_display_and_lvgl() {
  if (g_screenshot_fb == nullptr) {
    g_screenshot_fb = static_cast<uint16_t *>(
        heap_caps_malloc(static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_screenshot_fb == nullptr) {
      g_screenshot_fb = static_cast<uint16_t *>(
          malloc(static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t)));
    }
    if (g_screenshot_fb != nullptr) {
      memset(g_screenshot_fb, 0, static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(uint16_t));
    }
  }

  esp_lcd_rgb_panel_config_t panel_config = {};
  panel_config.clk_src = LCD_CLK_SRC_PLL160M;
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

  int pins[16] = {45, 48, 47, 21, 14, 5, 6, 7, 15, 16, 4, 8, 3, 46, 9, 1};
  for (size_t i = 0; i < 16; ++i) {
    panel_config.data_gpio_nums[i] = pins[i];
  }

  panel_config.psram_trans_align = 64;
  panel_config.sram_trans_align = 4;
  panel_config.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&panel_config, &g_panel) == ESP_OK) {
    esp_lcd_panel_reset(g_panel);
    esp_lcd_panel_init(g_panel);
  }

  lv_init();

  const size_t buf_pixels = kDisplayWidth * 40;
  g_buf_1 = static_cast<lv_color_t *>(
      heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_buf_2 = static_cast<lv_color_t *>(
      heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (g_buf_1 == nullptr || g_buf_2 == nullptr) {
    g_buf_1 = static_cast<lv_color_t *>(malloc(buf_pixels * sizeof(lv_color_t)));
    g_buf_2 = static_cast<lv_color_t *>(malloc(buf_pixels * sizeof(lv_color_t)));
  }

  lv_disp_draw_buf_init(&g_draw_buf, g_buf_1, g_buf_2, buf_pixels);
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kDisplayWidth;
  g_disp_drv.ver_res = kDisplayHeight;
  g_disp_drv.flush_cb = rgb_flush_cb;
  g_disp_drv.draw_buf = &g_draw_buf;
  lv_disp_drv_register(&g_disp_drv);

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&g_indev_drv);
}

void init_backlight() {
  ledcSetup(kBacklightChannel, kBacklightFreq, kBacklightResolution);
  ledcAttachPin(kBacklightPin, kBacklightChannel);
  ledcWrite(kBacklightChannel, 255);
}

void init_sensors() {
  g_touch_i2c.begin(kTouchI2cSda, kTouchI2cScl, 400000U);
  g_sensor_i2c.begin(kSensorI2cSda, kSensorI2cScl, 100000U);
  g_aht_ready = g_aht.begin(&g_sensor_i2c);
}

void init_network() {
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(wifi_event_handler);
  WiFi.setAutoReconnect(true);
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
  if (!g_have_outdoor_temp && !g_have_weather_condition) {
    g_runtime->on_outdoor_weather_update(6.0f, "Cloudy");
  } else {
    g_runtime->on_outdoor_weather_update(g_outdoor_temp_c, g_weather_condition);
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

  g_runtime->begin();
  g_runtime->set_local_temperature_compensation_c(g_cfg_temp_comp_c);
  g_runtime->set_temperature_unit(g_cfg_temp_unit_f ? TemperatureUnit::Fahrenheit
                                                    : TemperatureUnit::Celsius);
}

void thermostat_firmware_loop() {
  const uint32_t now = millis();

  if ((now - g_last_ui_tick_ms) >= kUiTickMs) {
    g_last_ui_tick_ms = now;
    lv_timer_handler();
  }

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
