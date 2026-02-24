#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "controller/controller_relay_io.h"
#include "controller/controller_node.h"
#include "controller/pirateweather.h"
#include "weather_icon.h"
#include "command_builder.h"
#include "espnow_cmd_word.h"
#include "management_paths.h"
#include "mqtt_payload.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <esp_system.h>
#include "ota_web_updater.h"

thermostat::ControllerNode *g_controller = nullptr;
thermostat::ControllerRelayIo g_relay_io;
WiFiClient g_ctrl_wifi_client;
PubSubClient g_ctrl_mqtt(g_ctrl_wifi_client);
WebServer g_ctrl_web(80);
uint32_t g_ctrl_last_wifi_attempt_ms = 0;
uint32_t g_ctrl_last_mqtt_attempt_ms = 0;
uint32_t g_ctrl_last_mqtt_publish_ms = 0;
bool g_ctrl_last_lockout = false;
bool g_ctrl_have_lockout = false;
uint32_t g_ctrl_last_mqtt_command_ms = 0;
String g_ctrl_last_mqtt_error = "none";
String g_ctrl_last_ota_error = "none";
String g_ctrl_last_espnow_error = "none";
uint16_t g_ctrl_mqtt_seq = 0;
bool g_ctrl_mqtt_discovery_sent = false;
bool g_ctrl_have_shadow = false;
FurnaceMode g_ctrl_shadow_mode = FurnaceMode::Off;
FanMode g_ctrl_shadow_fan = FanMode::Automatic;
float g_ctrl_shadow_setpoint_c = 20.0f;
bool g_ctrl_ota_started = false;
bool g_ctrl_web_started = false;
bool g_ctrl_mdns_started = false;
uint32_t g_ctrl_boot_count = 0;
String g_ctrl_reset_reason = "unknown";
bool g_ctrl_reboot_requested = false;
uint32_t g_ctrl_reboot_at_ms = 0;

constexpr uint32_t kCtrlNetworkRetryMs = 5000;
constexpr uint32_t kCtrlMqttPublishMs = 10000;
constexpr uint32_t kCtrlMqttPrimaryHoldMs = 30000;
constexpr uint32_t kCtrlWeatherPollMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kCtrlHttpTimeoutMs = 8000;
uint32_t g_ctrl_last_weather_poll_ms = 0;

#ifndef THERMOSTAT_CONTROLLER_WIFI_SSID
#define THERMOSTAT_CONTROLLER_WIFI_SSID ""
#endif

#ifndef THERMOSTAT_CONTROLLER_WIFI_PASSWORD
#define THERMOSTAT_CONTROLLER_WIFI_PASSWORD ""
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_HOST
#define THERMOSTAT_CONTROLLER_MQTT_HOST "mqtt.lan"
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_PORT
#define THERMOSTAT_CONTROLLER_MQTT_PORT 1883
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_USER
#define THERMOSTAT_CONTROLLER_MQTT_USER ""
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_PASSWORD
#define THERMOSTAT_CONTROLLER_MQTT_PASSWORD ""
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID
#define THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID "esp32-furnace-controller"
#endif

#ifndef THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC
#define THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC "thermostat/furnace-controller"
#endif

#ifndef THERMOSTAT_THERMOSTAT_MQTT_BASE_TOPIC
#define THERMOSTAT_THERMOSTAT_MQTT_BASE_TOPIC "thermostat/furnace-display"
#endif

#ifndef THERMOSTAT_MQTT_SHARED_DEVICE_ID
#define THERMOSTAT_MQTT_SHARED_DEVICE_ID "wireless_thermostat_system"
#endif

#ifndef THERMOSTAT_CONTROLLER_ESPNOW_PEER_MAC
#define THERMOSTAT_CONTROLLER_ESPNOW_PEER_MAC "FF:FF:FF:FF:FF:FF"
#endif

#ifndef THERMOSTAT_CONTROLLER_ESPNOW_PEER_MACS
#define THERMOSTAT_CONTROLLER_ESPNOW_PEER_MACS ""
#endif

#ifndef THERMOSTAT_CONTROLLER_ESPNOW_LMK
#define THERMOSTAT_CONTROLLER_ESPNOW_LMK "a1b2c3d4e5f60718293a4b5c6d7e8f90"
#endif

#ifndef THERMOSTAT_CONTROLLER_ESPNOW_CHANNEL
#define THERMOSTAT_CONTROLLER_ESPNOW_CHANNEL 6
#endif

#ifndef THERMOSTAT_CONTROLLER_OTA_HOSTNAME
#define THERMOSTAT_CONTROLLER_OTA_HOSTNAME "esp32-furnace-controller"
#endif

#ifndef THERMOSTAT_CONTROLLER_OTA_PASSWORD
#define THERMOSTAT_CONTROLLER_OTA_PASSWORD ""
#endif

#ifndef THERMOSTAT_FIRMWARE_VERSION
#define THERMOSTAT_FIRMWARE_VERSION "dev"
#endif

#ifndef THERMOSTAT_CONTROLLER_PIRATEWEATHER_API_KEY
#define THERMOSTAT_CONTROLLER_PIRATEWEATHER_API_KEY ""
#endif

#ifndef THERMOSTAT_CONTROLLER_PIRATEWEATHER_ZIP
#define THERMOSTAT_CONTROLLER_PIRATEWEATHER_ZIP ""
#endif

Preferences g_ctrl_cfg;
bool g_ctrl_cfg_ready = false;
String g_cfg_ctrl_wifi_ssid = THERMOSTAT_CONTROLLER_WIFI_SSID;
String g_cfg_ctrl_wifi_password = THERMOSTAT_CONTROLLER_WIFI_PASSWORD;
String g_cfg_ctrl_mqtt_host = THERMOSTAT_CONTROLLER_MQTT_HOST;
uint16_t g_cfg_ctrl_mqtt_port = THERMOSTAT_CONTROLLER_MQTT_PORT;
String g_cfg_ctrl_mqtt_user = THERMOSTAT_CONTROLLER_MQTT_USER;
String g_cfg_ctrl_mqtt_password = THERMOSTAT_CONTROLLER_MQTT_PASSWORD;
String g_cfg_ctrl_mqtt_client_id = THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID;
String g_cfg_ctrl_mqtt_base_topic = THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC;
String g_cfg_display_mqtt_base_topic = THERMOSTAT_THERMOSTAT_MQTT_BASE_TOPIC;
String g_cfg_shared_device_id = THERMOSTAT_MQTT_SHARED_DEVICE_ID;
String g_cfg_ctrl_ota_hostname = THERMOSTAT_CONTROLLER_OTA_HOSTNAME;
String g_cfg_ctrl_ota_password = THERMOSTAT_CONTROLLER_OTA_PASSWORD;
uint8_t g_cfg_ctrl_espnow_channel = THERMOSTAT_CONTROLLER_ESPNOW_CHANNEL;
String g_cfg_ctrl_espnow_peer_mac = THERMOSTAT_CONTROLLER_ESPNOW_PEER_MAC;
String g_cfg_ctrl_espnow_peer_macs = THERMOSTAT_CONTROLLER_ESPNOW_PEER_MACS;
String g_cfg_ctrl_espnow_lmk = THERMOSTAT_CONTROLLER_ESPNOW_LMK;
String g_cfg_ctrl_primary_sensor_mac = "FF:FF:FF:FF:FF:FF";
String g_cfg_ctrl_pirateweather_api_key = THERMOSTAT_CONTROLLER_PIRATEWEATHER_API_KEY;
String g_cfg_ctrl_pirateweather_zip = THERMOSTAT_CONTROLLER_PIRATEWEATHER_ZIP;
bool g_ctrl_mqtt_reconfigure_required = false;
bool g_ctrl_wifi_reconnect_required = false;
bool g_ctrl_cfg_reboot_required = false;
String g_disp_availability = "unknown";

struct CtrlDisplayCfgCacheEntry {
  const char *key;
  String value;
};

CtrlDisplayCfgCacheEntry g_disp_cfg_cache[] = {
    {"wifi_ssid", "unknown"},
    {"wifi_password", "unknown"},
    {"mqtt_host", "unknown"},
    {"mqtt_port", "unknown"},
    {"mqtt_user", "unknown"},
    {"mqtt_password", "unknown"},
    {"mqtt_client_id", "unknown"},
    {"mqtt_base_topic", "unknown"},
    {"discovery_prefix", "unknown"},
    {"shared_device_id", "unknown"},
    {"pirateweather_api_key", "unknown"},
    {"pirateweather_zip", "unknown"},
    {"display_timeout_s", "unknown"},
    {"temp_comp_c", "unknown"},
    {"temperature_unit", "unknown"},
    {"ota_hostname", "unknown"},
    {"ota_password", "unknown"},
    {"espnow_channel", "unknown"},
    {"espnow_peer_mac", "unknown"},
    {"espnow_lmk", "unknown"},
    {"controller_base_topic", "unknown"},
    {"controller_timeout_ms", "unknown"},
    {"reboot_required", "unknown"},
};

void ctrl_load_runtime_config() {
  if (!g_ctrl_cfg_ready) {
    return;
  }
  g_cfg_ctrl_wifi_ssid = g_ctrl_cfg.getString("wifi_ssid", g_cfg_ctrl_wifi_ssid);
  g_cfg_ctrl_wifi_password = g_ctrl_cfg.getString("wifi_pwd", g_cfg_ctrl_wifi_password);
  g_cfg_ctrl_mqtt_host = g_ctrl_cfg.getString("mqtt_host", g_cfg_ctrl_mqtt_host);
  g_cfg_ctrl_mqtt_port = static_cast<uint16_t>(g_ctrl_cfg.getUInt("mqtt_port", g_cfg_ctrl_mqtt_port));
  g_cfg_ctrl_mqtt_user = g_ctrl_cfg.getString("mqtt_user", g_cfg_ctrl_mqtt_user);
  g_cfg_ctrl_mqtt_password = g_ctrl_cfg.getString("mqtt_pwd", g_cfg_ctrl_mqtt_password);
  g_cfg_ctrl_mqtt_client_id = g_ctrl_cfg.getString("mqtt_cid", g_cfg_ctrl_mqtt_client_id);
  g_cfg_ctrl_mqtt_base_topic = g_ctrl_cfg.getString("mqtt_base", g_cfg_ctrl_mqtt_base_topic);
  g_cfg_display_mqtt_base_topic = g_ctrl_cfg.getString("disp_base", g_cfg_display_mqtt_base_topic);
  g_cfg_shared_device_id = g_ctrl_cfg.getString("shared_id", g_cfg_shared_device_id);
  g_cfg_ctrl_ota_hostname = g_ctrl_cfg.getString("ota_host", g_cfg_ctrl_ota_hostname);
  g_cfg_ctrl_ota_password = g_ctrl_cfg.getString("ota_pwd", g_cfg_ctrl_ota_password);
  g_cfg_ctrl_espnow_channel = static_cast<uint8_t>(g_ctrl_cfg.getUChar("esp_ch", g_cfg_ctrl_espnow_channel));
  g_cfg_ctrl_espnow_peer_mac = g_ctrl_cfg.getString("esp_peer", g_cfg_ctrl_espnow_peer_mac);
  g_cfg_ctrl_espnow_peer_macs = g_ctrl_cfg.getString("esp_peers", g_cfg_ctrl_espnow_peer_macs);
  g_cfg_ctrl_espnow_lmk = g_ctrl_cfg.getString("esp_lmk", g_cfg_ctrl_espnow_lmk);
  g_cfg_ctrl_primary_sensor_mac = g_ctrl_cfg.getString("pri_sensor", g_cfg_ctrl_primary_sensor_mac);
  g_cfg_ctrl_pirateweather_api_key = g_ctrl_cfg.getString("pw_key", g_cfg_ctrl_pirateweather_api_key);
  g_cfg_ctrl_pirateweather_zip = g_ctrl_cfg.getString("pw_zip", g_cfg_ctrl_pirateweather_zip);
}

String ctrl_topic_for(const char *suffix) {
  String out(g_cfg_ctrl_mqtt_base_topic);
  out += "/";
  out += suffix;
  return out;
}

String display_topic_for(const char *suffix) {
  String out(g_cfg_display_mqtt_base_topic);
  out += "/";
  out += suffix;
  return out;
}

void ctrl_set_display_cfg_cache(const String &key, const String &value) {
  for (size_t i = 0; i < (sizeof(g_disp_cfg_cache) / sizeof(g_disp_cfg_cache[0])); ++i) {
    if (key == g_disp_cfg_cache[i].key) {
      g_disp_cfg_cache[i].value = value;
      return;
    }
  }
}

String ctrl_get_display_cfg_cache(const char *key) {
  for (size_t i = 0; i < (sizeof(g_disp_cfg_cache) / sizeof(g_disp_cfg_cache[0])); ++i) {
    if (strcmp(key, g_disp_cfg_cache[i].key) == 0) {
      return g_disp_cfg_cache[i].value;
    }
  }
  return "unknown";
}

bool ctrl_publish_display_cfg_set(const String &key, const String &value) {
  if (!g_ctrl_mqtt.connected()) {
    return false;
  }
  String topic = display_topic_for("cfg");
  topic += "/";
  topic += key;
  topic += "/set";
  return g_ctrl_mqtt.publish(topic.c_str(), value.c_str(), true);
}

bool ctrl_publish_display_cmd(const String &key, const String &value) {
  if (!g_ctrl_mqtt.connected()) {
    return false;
  }
  String topic = display_topic_for("cmd");
  topic += "/";
  topic += key;
  return g_ctrl_mqtt.publish(topic.c_str(), value.c_str(), true);
}

void ctrl_schedule_reboot() {
  g_ctrl_reboot_requested = true;
  g_ctrl_reboot_at_ms = millis() + 250;
}

void ctrl_publish_cfg_value(const char *key, const String &value) {
  if (!g_ctrl_mqtt.connected()) {
    return;
  }
  String topic = ctrl_topic_for("cfg");
  topic += "/";
  topic += key;
  topic += "/state";
  const char *payload = value.c_str();
  if (thermostat::management_paths::is_secret_cfg_key(key)) {
    payload = value.length() > 0 ? "set" : "unset";
  }
  g_ctrl_mqtt.publish(topic.c_str(), payload, true);
}

void ctrl_publish_all_cfg_state() {
  ctrl_publish_cfg_value("wifi_ssid", g_cfg_ctrl_wifi_ssid);
  ctrl_publish_cfg_value("wifi_password", g_cfg_ctrl_wifi_password);
  ctrl_publish_cfg_value("mqtt_host", g_cfg_ctrl_mqtt_host);
  ctrl_publish_cfg_value("mqtt_port", String(g_cfg_ctrl_mqtt_port));
  ctrl_publish_cfg_value("mqtt_user", g_cfg_ctrl_mqtt_user);
  ctrl_publish_cfg_value("mqtt_password", g_cfg_ctrl_mqtt_password);
  ctrl_publish_cfg_value("mqtt_client_id", g_cfg_ctrl_mqtt_client_id);
  ctrl_publish_cfg_value("mqtt_base_topic", g_cfg_ctrl_mqtt_base_topic);
  ctrl_publish_cfg_value("display_mqtt_base_topic", g_cfg_display_mqtt_base_topic);
  ctrl_publish_cfg_value("shared_device_id", g_cfg_shared_device_id);
  ctrl_publish_cfg_value("ota_hostname", g_cfg_ctrl_ota_hostname);
  ctrl_publish_cfg_value("ota_password", g_cfg_ctrl_ota_password);
  ctrl_publish_cfg_value("espnow_channel", String(g_cfg_ctrl_espnow_channel));
  ctrl_publish_cfg_value("espnow_peer_mac", g_cfg_ctrl_espnow_peer_mac);
  ctrl_publish_cfg_value("espnow_peer_macs", g_cfg_ctrl_espnow_peer_macs);
  ctrl_publish_cfg_value("espnow_lmk", g_cfg_ctrl_espnow_lmk);
  ctrl_publish_cfg_value("primary_sensor_mac", g_cfg_ctrl_primary_sensor_mac);
  ctrl_publish_cfg_value("pirateweather_api_key", g_cfg_ctrl_pirateweather_api_key);
  ctrl_publish_cfg_value("pirateweather_zip", g_cfg_ctrl_pirateweather_zip);
  ctrl_publish_cfg_value("reboot_required", g_ctrl_cfg_reboot_required ? "1" : "0");
}

bool ctrl_parse_mac(const char *text, uint8_t out[6]);

bool ctrl_try_update_runtime_config(const String &key, const char *raw_value) {
  if (!g_ctrl_cfg_ready || raw_value == nullptr) {
    return false;
  }
  const String value(raw_value);
  bool known = true;
  if (key == "wifi_ssid") {
    g_cfg_ctrl_wifi_ssid = value;
    g_ctrl_cfg.putString("wifi_ssid", value);
    g_ctrl_wifi_reconnect_required = true;
  } else if (key == "wifi_password") {
    g_cfg_ctrl_wifi_password = value;
    g_ctrl_cfg.putString("wifi_pwd", value);
    g_ctrl_wifi_reconnect_required = true;
  } else if (key == "mqtt_host") {
    g_cfg_ctrl_mqtt_host = value;
    g_ctrl_cfg.putString("mqtt_host", value);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_port") {
    const long parsed = atol(raw_value);
    if (parsed < 1 || parsed > 65535) {
      return false;
    }
    g_cfg_ctrl_mqtt_port = static_cast<uint16_t>(parsed);
    g_ctrl_cfg.putUInt("mqtt_port", g_cfg_ctrl_mqtt_port);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_user") {
    g_cfg_ctrl_mqtt_user = value;
    g_ctrl_cfg.putString("mqtt_user", value);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_password") {
    g_cfg_ctrl_mqtt_password = value;
    g_ctrl_cfg.putString("mqtt_pwd", value);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_client_id") {
    g_cfg_ctrl_mqtt_client_id = value;
    g_ctrl_cfg.putString("mqtt_cid", value);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "mqtt_base_topic") {
    g_cfg_ctrl_mqtt_base_topic = value;
    g_ctrl_cfg.putString("mqtt_base", value);
    g_ctrl_mqtt_reconfigure_required = true;
    g_ctrl_mqtt_discovery_sent = false;
  } else if (key == "display_mqtt_base_topic") {
    g_cfg_display_mqtt_base_topic = value;
    g_ctrl_cfg.putString("disp_base", value);
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "shared_device_id") {
    g_cfg_shared_device_id = value;
    g_ctrl_cfg.putString("shared_id", value);
    g_ctrl_mqtt_discovery_sent = false;
  } else if (key == "ota_hostname") {
    g_cfg_ctrl_ota_hostname = value;
    g_ctrl_cfg.putString("ota_host", value);
  } else if (key == "ota_password") {
    g_cfg_ctrl_ota_password = value;
    g_ctrl_cfg.putString("ota_pwd", value);
  } else if (key == "espnow_channel") {
    const long parsed = atol(raw_value);
    if (parsed < 1 || parsed > 14) {
      return false;
    }
    g_cfg_ctrl_espnow_channel = static_cast<uint8_t>(parsed);
    g_ctrl_cfg.putUChar("esp_ch", g_cfg_ctrl_espnow_channel);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "espnow_peer_mac") {
    g_cfg_ctrl_espnow_peer_mac = value;
    g_ctrl_cfg.putString("esp_peer", value);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "espnow_peer_macs") {
    g_cfg_ctrl_espnow_peer_macs = value;
    g_ctrl_cfg.putString("esp_peers", value);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "espnow_lmk") {
    g_cfg_ctrl_espnow_lmk = value;
    g_ctrl_cfg.putString("esp_lmk", value);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "primary_sensor_mac") {
    uint8_t parsed_mac[6];
    if (!ctrl_parse_mac(raw_value, parsed_mac)) {
      return false;
    }
    g_cfg_ctrl_primary_sensor_mac = value;
    g_ctrl_cfg.putString("pri_sensor", value);
    if (g_controller != nullptr) {
      g_controller->app().set_primary_sensor_mac(parsed_mac);
    }
  } else if (key == "pirateweather_api_key") {
    g_cfg_ctrl_pirateweather_api_key = value;
    g_ctrl_cfg.putString("pw_key", value);
    g_ctrl_last_weather_poll_ms = 0;
  } else if (key == "pirateweather_zip") {
    g_cfg_ctrl_pirateweather_zip = value;
    g_ctrl_cfg.putString("pw_zip", value);
    g_ctrl_last_weather_poll_ms = 0;
  } else {
    known = false;
  }

  if (!known) {
    return false;
  }
  if (g_ctrl_mqtt.connected()) {
    ctrl_publish_cfg_value(key.c_str(), value);
  }
  return true;
}

bool ctrl_parse_u32_payload(const char *value, uint32_t *out) {
  if (value == nullptr || out == nullptr || value[0] == '\0') {
    return false;
  }
  errno = 0;
  char *end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 0);
  if (errno != 0 || end == value || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

const char *ctrl_reset_reason_text(esp_reset_reason_t reason) {
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

bool ctrl_parse_mac(const char *text, uint8_t out[6]) {
  if (text == nullptr || strlen(text) < 17) {
    return false;
  }
  unsigned values[6] = {};
  if (sscanf(text, "%02x:%02x:%02x:%02x:%02x:%02x", &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    out[i] = static_cast<uint8_t>(values[i] & 0xFFu);
  }
  return true;
}

bool ctrl_is_broadcast_mac(const uint8_t mac[6]) {
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, kBroadcast, sizeof(kBroadcast)) == 0;
}

// Parse sensor topic: {base}/sensor/{mac}/{suffix}
// Returns true if topic matches, setting mac_out and suffix_out.
bool ctrl_parse_sensor_topic(const char *base, const char *topic,
                             char mac_out[18], const char **suffix_out) {
  const size_t base_len = strlen(base);
  if (strncmp(topic, base, base_len) != 0) return false;
  // Expect "/sensor/" after base
  if (strncmp(topic + base_len, "/sensor/", 8) != 0) return false;
  const char *mac_start = topic + base_len + 8;
  // MAC is 17 chars "AA:BB:CC:DD:EE:FF"
  if (strlen(mac_start) < 18 || mac_start[17] != '/') return false;
  memcpy(mac_out, mac_start, 17);
  mac_out[17] = '\0';
  *suffix_out = mac_start + 18;
  return true;
}

bool ctrl_parse_lmk_hex(const char *text, uint8_t out[16]) {
  if (text == nullptr || strlen(text) != 32) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    unsigned byte = 0;
    if (sscanf(text + (i * 2), "%02x", &byte) != 1) {
      return false;
    }
    out[i] = static_cast<uint8_t>(byte & 0xFFu);
  }
  return true;
}

// --- Weather polling (PirateWeather API) ---

bool ctrl_fetch_zip_coordinates(const char *zip, float *lat_out, float *lon_out) {
  if (lat_out == nullptr || lon_out == nullptr || zip[0] == '\0') return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const std::string url = pirateweather::geocode_url(zip);
  if (!http.begin(client, url.c_str())) return false;
  http.setTimeout(kCtrlHttpTimeoutMs);
  const int status = http.GET();
  if (status != 200) { http.end(); return false; }
  const String body = http.getString();
  http.end();
  return pirateweather::parse_geocode_response(body.c_str(), lat_out, lon_out);
}

bool ctrl_fetch_pirateweather_current(float lat, float lon, float *temp_c_out,
                                       thermostat::WeatherIcon *icon_out) {
  if (temp_c_out == nullptr || icon_out == nullptr ||
      g_cfg_ctrl_pirateweather_api_key.length() == 0) {
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  const std::string url = pirateweather::forecast_url(
      g_cfg_ctrl_pirateweather_api_key.c_str(), lat, lon);
  if (!http.begin(client, url.c_str())) return false;
  http.setTimeout(kCtrlHttpTimeoutMs);
  const int status = http.GET();
  if (status != 200) { http.end(); return false; }
  const String body = http.getString();
  http.end();
  return pirateweather::parse_forecast_response(body.c_str(), temp_c_out,
                                                icon_out);
}

void ctrl_poll_weather(uint32_t now_ms) {
  if (WiFi.status() != WL_CONNECTED || g_controller == nullptr) return;
  if (g_cfg_ctrl_pirateweather_api_key.length() == 0 ||
      g_cfg_ctrl_pirateweather_zip.length() == 0) {
    return;
  }
  if (g_ctrl_last_weather_poll_ms != 0 &&
      (now_ms - g_ctrl_last_weather_poll_ms) < kCtrlWeatherPollMs) {
    return;
  }
  g_ctrl_last_weather_poll_ms = now_ms;

  const std::string zip = pirateweather::normalize_zip(
      g_cfg_ctrl_pirateweather_zip.c_str());
  if (zip.empty()) return;

  float lat = 0.0f, lon = 0.0f;
  if (!ctrl_fetch_zip_coordinates(zip.c_str(), &lat, &lon)) return;

  float outdoor_temp_c = 0.0f;
  thermostat::WeatherIcon icon = thermostat::WeatherIcon::Unknown;
  if (!ctrl_fetch_pirateweather_current(lat, lon, &outdoor_temp_c, &icon)) return;

  // Store in app for MQTT publishing
  g_controller->app().set_outdoor_weather(outdoor_temp_c, icon);

  // Send to all connected displays via ESP-NOW
  g_controller->transport().publish_weather(outdoor_temp_c, icon);

  // Publish to MQTT
  if (g_ctrl_mqtt.connected()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", outdoor_temp_c);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/outdoor_temp_c").c_str(), buf, true);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/weather_condition").c_str(),
                        thermostat::weather_icon_display_text(icon), true);
  }
}

void ctrl_publish_discovery() {
  if (!g_ctrl_mqtt.connected() || g_ctrl_mqtt_discovery_sent) {
    return;
  }

  const String base = g_cfg_ctrl_mqtt_base_topic;
  const String dev_id = g_cfg_shared_device_id;
  const String switch_topic = String("homeassistant/switch/") + dev_id + "_lockout/config";
  const String filter_topic = String("homeassistant/sensor/") + dev_id + "_filter_runtime/config";
  const String state_topic = String("homeassistant/sensor/") + dev_id + "_furnace_state/config";
  const String fw_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_firmware/config";
  const String rssi_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_wifi_rssi/config";
  const String heap_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_free_heap/config";
  const String last_mqtt_cmd_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_last_mqtt_command/config";
  const String last_espnow_rx_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_last_espnow_rx/config";
  const String espnow_ok_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_espnow_send_ok/config";
  const String espnow_fail_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_espnow_send_fail/config";
  const String err_mqtt_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_error_mqtt/config";
  const String err_ota_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_error_ota/config";
  const String err_espnow_topic =
      String("homeassistant/sensor/") + dev_id + "_controller_error_espnow/config";
  const String reset_seq_topic =
      String("homeassistant/button/") + dev_id + "_controller_reset_sequence/config";
  const String reboot_topic =
      String("homeassistant/button/") + dev_id + "_controller_reboot/config";
  const String filter_change_topic =
      String("homeassistant/binary_sensor/") + dev_id + "_filter_change_required/config";

  const String climate_topic = String("homeassistant/climate/") + dev_id + "/config";

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
      dev_id.c_str(), base.c_str(), base.c_str(), base.c_str(), base.c_str(), base.c_str(),
      base.c_str(), base.c_str(), base.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(climate_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"HVAC Lockout\",\"uniq_id\":\"%s_lockout\",\"cmd_t\":\"%s/cmd/lockout\","
           "\"stat_t\":\"%s/state/lockout\",\"pl_on\":\"1\",\"pl_off\":\"0\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(switch_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Filter Runtime\",\"uniq_id\":\"%s_filter_runtime\","
           "\"stat_t\":\"%s/state/filter_runtime_hours\",\"unit_of_meas\":\"h\","
           "\"dev_cla\":\"duration\",\"stat_cla\":\"measurement\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(filter_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Furnace State\",\"uniq_id\":\"%s_furnace_state\","
           "\"stat_t\":\"%s/state/furnace_state\",\"icon\":\"mdi:hvac\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(state_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Firmware Version\",\"uniq_id\":\"%s_controller_firmware\","
           "\"stat_t\":\"%s/state/firmware_version\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(fw_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller WiFi RSSI\",\"uniq_id\":\"%s_controller_wifi_rssi\","
           "\"stat_t\":\"%s/state/wifi_rssi\",\"unit_of_meas\":\"dBm\","
           "\"dev_cla\":\"signal_strength\",\"stat_cla\":\"measurement\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(rssi_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Free Heap\",\"uniq_id\":\"%s_controller_free_heap\","
           "\"stat_t\":\"%s/state/free_heap_bytes\",\"unit_of_meas\":\"B\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(heap_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Last MQTT Command\",\"uniq_id\":\"%s_controller_last_mqtt_command\","
           "\"stat_t\":\"%s/state/last_mqtt_command_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(last_mqtt_cmd_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Last ESP-NOW RX\",\"uniq_id\":\"%s_controller_last_espnow_rx\","
           "\"stat_t\":\"%s/state/last_espnow_rx_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(last_espnow_rx_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller ESP-NOW Send OK\",\"uniq_id\":\"%s_controller_espnow_send_ok\","
           "\"stat_t\":\"%s/state/espnow_send_ok_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(espnow_ok_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller ESP-NOW Send Fail\",\"uniq_id\":\"%s_controller_espnow_send_fail\","
           "\"stat_t\":\"%s/state/espnow_send_fail_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(espnow_fail_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller MQTT Error\",\"uniq_id\":\"%s_controller_error_mqtt\","
           "\"stat_t\":\"%s/state/error_mqtt\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(err_mqtt_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller OTA Error\",\"uniq_id\":\"%s_controller_error_ota\","
           "\"stat_t\":\"%s/state/error_ota\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(err_ota_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller ESP-NOW Error\",\"uniq_id\":\"%s_controller_error_espnow\","
           "\"stat_t\":\"%s/state/error_espnow\",\"entity_category\":\"diagnostic\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(err_espnow_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Reset Command Sequence\","
           "\"uniq_id\":\"%s_controller_reset_sequence\","
           "\"cmd_t\":\"%s/cmd/reset_sequence\",\"pl_prs\":\"1\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(reset_seq_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Reboot\","
           "\"uniq_id\":\"%s_controller_reboot\","
           "\"cmd_t\":\"%s/cmd/reboot\",\"pl_prs\":\"1\","
           "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(reboot_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Filter Change Required\",\"uniq_id\":\"%s_filter_change_required\","
           "\"stat_t\":\"%s/state/filter_change_required\","
           "\"dev_cla\":\"problem\",\"pl_on\":\"1\",\"pl_off\":\"0\","
           "\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(filter_change_topic.c_str(), payload, true);

  g_ctrl_mqtt_discovery_sent = true;
}

String ctrl_json_escape(const String &in) {
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

void ctrl_web_handle_config_get() {
  String body = "{\"controller\":{";
  body += "\"wifi_ssid\":\"" + ctrl_json_escape(g_cfg_ctrl_wifi_ssid) + "\",";
  body += "\"wifi_password\":\"" + String(g_cfg_ctrl_wifi_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_host\":\"" + ctrl_json_escape(g_cfg_ctrl_mqtt_host) + "\",";
  body += "\"mqtt_port\":" + String(g_cfg_ctrl_mqtt_port) + ",";
  body += "\"mqtt_user\":\"" + ctrl_json_escape(g_cfg_ctrl_mqtt_user) + "\",";
  body += "\"mqtt_password\":\"" + String(g_cfg_ctrl_mqtt_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_client_id\":\"" + ctrl_json_escape(g_cfg_ctrl_mqtt_client_id) + "\",";
  body += "\"mqtt_base_topic\":\"" + ctrl_json_escape(g_cfg_ctrl_mqtt_base_topic) + "\",";
  body += "\"display_mqtt_base_topic\":\"" + ctrl_json_escape(g_cfg_display_mqtt_base_topic) + "\",";
  body += "\"shared_device_id\":\"" + ctrl_json_escape(g_cfg_shared_device_id) + "\",";
  body += "\"ota_hostname\":\"" + ctrl_json_escape(g_cfg_ctrl_ota_hostname) + "\",";
  body += "\"ota_password\":\"" + String(g_cfg_ctrl_ota_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"espnow_channel\":" + String(g_cfg_ctrl_espnow_channel) + ",";
  body += "\"espnow_peer_mac\":\"" + ctrl_json_escape(g_cfg_ctrl_espnow_peer_mac) + "\",";
  body += "\"espnow_peer_macs\":\"" + ctrl_json_escape(g_cfg_ctrl_espnow_peer_macs) + "\",";
  body += "\"espnow_lmk\":\"" + String(g_cfg_ctrl_espnow_lmk.length() > 0 ? "set" : "unset") + "\",";
  body += "\"primary_sensor_mac\":\"" + ctrl_json_escape(g_cfg_ctrl_primary_sensor_mac) + "\",";
  body += "\"pirateweather_api_key\":\"" +
          String(g_cfg_ctrl_pirateweather_api_key.length() > 0 ? "set" : "unset") + "\",";
  body += "\"pirateweather_zip\":\"" + ctrl_json_escape(g_cfg_ctrl_pirateweather_zip) + "\",";
  body += "\"reboot_required\":" + String(g_ctrl_cfg_reboot_required ? "true" : "false");
  body += "},\"display\":{";
  body += "\"availability\":\"" + ctrl_json_escape(g_disp_availability) + "\",";
  for (size_t i = 0; i < (sizeof(g_disp_cfg_cache) / sizeof(g_disp_cfg_cache[0])); ++i) {
    body += "\"" + String(g_disp_cfg_cache[i].key) + "\":\"" +
            ctrl_json_escape(g_disp_cfg_cache[i].value) + "\"";
    if (i + 1 < (sizeof(g_disp_cfg_cache) / sizeof(g_disp_cfg_cache[0]))) {
      body += ",";
    }
  }
  body += "}}";
  g_ctrl_web.send(200, "application/json", body);
}

void ctrl_web_handle_config_post() {
  int updated_local = 0;
  int updated_display = 0;
  for (int i = 0; i < g_ctrl_web.args(); ++i) {
    const String name = g_ctrl_web.argName(i);
    const String value = g_ctrl_web.arg(i);
    if (name == "disp_reboot" && mqtt_payload::parse_bool(value.c_str())) {
      if (ctrl_publish_display_cmd("reboot", "1")) {
        ++updated_display;
      }
      continue;
    }
    std::string key_std;
    if (thermostat::management_paths::parse_prefixed_form_key(name.c_str(), "disp_",
                                                               &key_std)) {
      if (ctrl_publish_display_cfg_set(key_std.c_str(), value)) {
        ++updated_display;
      }
      continue;
    }
    if (ctrl_try_update_runtime_config(name, value.c_str())) {
      ++updated_local;
    }
  }
  String body = "updated_local=" + String(updated_local) +
                " updated_display=" + String(updated_display) + "\n";
  g_ctrl_web.send(200, "text/plain", body);
}

void ctrl_web_handle_reboot_post() {
  ctrl_schedule_reboot();
  g_ctrl_web.send(200, "text/plain", "rebooting\n");
}

void ctrl_web_handle_root() {
  String html;
  html.reserve(12288);
  html += "<html><body><h1>System Config (Controller + Display)</h1>";
  html += "<p><a href=\"/config\">JSON config</a> | <a href=\"/update\">Firmware Update</a></p>";
  html += "<h2>Controller</h2>";
  html += "<fieldset><legend>Networking Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"wifi_ssid\" maxlength=\"64\" value=\"" + g_cfg_ctrl_wifi_ssid + "\"><br>";
  html += "wifi_password: <input name=\"wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"mqtt_host\" value=\"" + g_cfg_ctrl_mqtt_host + "\"><br>";
  html += "mqtt_port: <input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" step=\"1\" value=\"" +
          String(g_cfg_ctrl_mqtt_port) + "\"><br>";
  html += "mqtt_user: <input name=\"mqtt_user\" value=\"" + g_cfg_ctrl_mqtt_user + "\"><br>";
  html += "mqtt_password: <input name=\"mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"mqtt_client_id\" value=\"" + g_cfg_ctrl_mqtt_client_id + "\"><br>";
  html += "mqtt_base_topic: <input name=\"mqtt_base_topic\" value=\"" + g_cfg_ctrl_mqtt_base_topic + "\"><br>";
  html += "display_mqtt_base_topic: <input name=\"display_mqtt_base_topic\" value=\"" +
          g_cfg_display_mqtt_base_topic + "\"><br>";
  html += "espnow_channel: <input name=\"espnow_channel\" type=\"number\" min=\"1\" max=\"14\" step=\"1\" value=\"" +
          String(g_cfg_ctrl_espnow_channel) + "\"><br>";
  html += "espnow_peer_mac: <input name=\"espnow_peer_mac\" pattern=\"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$\" title=\"Format: AA:BB:CC:DD:EE:FF\" value=\"" + g_cfg_ctrl_espnow_peer_mac +
          "\"><br>";
  html += "espnow_peer_macs: <input name=\"espnow_peer_macs\" title=\"Comma-separated MACs: AA:BB:CC:DD:EE:FF,11:22:33:44:55:66\" value=\"" + g_cfg_ctrl_espnow_peer_macs +
          "\"><br>";
  html += "espnow_lmk: <input name=\"espnow_lmk\" pattern=\"^[0-9A-Fa-f]{32}$\" title=\"32 hex characters\" value=\"\"><br>";
  html += "<button type=\"submit\">Save Networking</button></form></fieldset>";

  html += "<fieldset><legend>Hardware Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "primary_sensor_mac: <input name=\"primary_sensor_mac\" pattern=\"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$\" title=\"Format: AA:BB:CC:DD:EE:FF (FF:FF:FF:FF:FF:FF = accept all)\" value=\"" + g_cfg_ctrl_primary_sensor_mac + "\"><br>";
  html += "ota_hostname: <input name=\"ota_hostname\" value=\"" + g_cfg_ctrl_ota_hostname + "\"><br>";
  html += "ota_password: <input name=\"ota_password\" value=\"\"><br>";
  html += "<button type=\"submit\">Save Hardware</button></form></fieldset>";

  html += "<fieldset><legend>Display Settings</legend>";
  html += "<p>Controller has no local display settings.</p>";
  html += "</fieldset>";

  html += "<fieldset><legend>Weather</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "pirateweather_api_key: <input name=\"pirateweather_api_key\" value=\"\"><br>";
  html += "pirateweather_zip: <input name=\"pirateweather_zip\" pattern=\"^[0-9]{5}(-[0-9]{4})?$\" title=\"US ZIP format: 12345 or 12345-6789\" value=\"" +
          g_cfg_ctrl_pirateweather_zip + "\"><br>";
  html += "<button type=\"submit\">Save Weather</button></form></fieldset>";

  html += "<fieldset><legend>Miscellaneous</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "shared_device_id: <input name=\"shared_device_id\" pattern=\"^[A-Za-z0-9_-]{1,64}$\" title=\"1-64 chars: letters, numbers, underscore, hyphen\" value=\"" + g_cfg_shared_device_id +
          "\"><br>";
  html += "<button type=\"submit\">Save Misc</button></form></fieldset>";

  html += "<p>reboot_required=" + String(g_ctrl_cfg_reboot_required ? "true" : "false") + "</p>";

  html += "<h2>Display (via MQTT)</h2>";
  html += "<p>availability=" + g_disp_availability + "</p>";
  html += "<fieldset><legend>Networking Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"disp_wifi_ssid\" maxlength=\"64\" value=\"" +
          ctrl_get_display_cfg_cache("wifi_ssid") + "\"><br>";
  html += "wifi_password: <input name=\"disp_wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"disp_mqtt_host\" value=\"" + ctrl_get_display_cfg_cache("mqtt_host") + "\"><br>";
  html += "mqtt_port: <input name=\"disp_mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" step=\"1\" value=\"" +
          ctrl_get_display_cfg_cache("mqtt_port") + "\"><br>";
  html += "mqtt_user: <input name=\"disp_mqtt_user\" value=\"" + ctrl_get_display_cfg_cache("mqtt_user") + "\"><br>";
  html += "mqtt_password: <input name=\"disp_mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"disp_mqtt_client_id\" value=\"" +
          ctrl_get_display_cfg_cache("mqtt_client_id") + "\"><br>";
  html += "mqtt_base_topic: <input name=\"disp_mqtt_base_topic\" value=\"" +
          ctrl_get_display_cfg_cache("mqtt_base_topic") + "\"><br>";
  html += "espnow_channel: <input name=\"disp_espnow_channel\" type=\"number\" min=\"1\" max=\"14\" step=\"1\" value=\"" +
          ctrl_get_display_cfg_cache("espnow_channel") + "\"><br>";
  html += "espnow_peer_mac: <input name=\"disp_espnow_peer_mac\" pattern=\"^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$\" title=\"Format: AA:BB:CC:DD:EE:FF\" value=\"" +
          ctrl_get_display_cfg_cache("espnow_peer_mac") + "\"><br>";
  html += "espnow_lmk: <input name=\"disp_espnow_lmk\" pattern=\"^[0-9A-Fa-f]{32}$\" title=\"32 hex characters\" value=\"\"><br>";
  html += "controller_base_topic: <input name=\"disp_controller_base_topic\" value=\"" +
          ctrl_get_display_cfg_cache("controller_base_topic") + "\"><br>";
  html += "<button type=\"submit\">Save Networking</button></form></fieldset>";

  html += "<fieldset><legend>Hardware Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "temp_comp_c: <input name=\"disp_temp_comp_c\" type=\"number\" min=\"-10\" max=\"10\" step=\"0.01\" value=\"" +
          ctrl_get_display_cfg_cache("temp_comp_c") + "\"><br>";
  html += "controller_timeout_ms: <input name=\"disp_controller_timeout_ms\" type=\"number\" min=\"1000\" max=\"600000\" step=\"1\" value=\"" +
          ctrl_get_display_cfg_cache("controller_timeout_ms") + "\"><br>";
  html += "ota_hostname: <input name=\"disp_ota_hostname\" value=\"" +
          ctrl_get_display_cfg_cache("ota_hostname") + "\"><br>";
  html += "ota_password: <input name=\"disp_ota_password\" value=\"\"><br>";
  html += "<button type=\"submit\">Save Hardware</button></form></fieldset>";

  html += "<fieldset><legend>Display Settings</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "display_timeout_s: <input name=\"disp_display_timeout_s\" type=\"number\" min=\"30\" max=\"600\" step=\"1\" value=\"" +
          ctrl_get_display_cfg_cache("display_timeout_s") + "\"><br>";
  html += "temperature_unit: <input name=\"disp_temperature_unit\" pattern=\"^(c|f|celsius|fahrenheit)$\" title=\"Use c, f, celsius, or fahrenheit\" value=\"" +
          ctrl_get_display_cfg_cache("temperature_unit") + "\"><br>";
  html += "<button type=\"submit\">Save Display</button></form></fieldset>";

  html += "<fieldset><legend>Weather</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "pirateweather_api_key: <input name=\"disp_pirateweather_api_key\" value=\"\"><br>";
  html += "pirateweather_zip: <input name=\"disp_pirateweather_zip\" pattern=\"^[0-9]{5}(-[0-9]{4})?$\" title=\"US ZIP format: 12345 or 12345-6789\" value=\"" +
          ctrl_get_display_cfg_cache("pirateweather_zip") + "\"><br>";
  html += "<button type=\"submit\">Save Weather</button></form></fieldset>";

  html += "<fieldset><legend>Miscellaneous</legend>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "discovery_prefix: <input name=\"disp_discovery_prefix\" value=\"" +
          ctrl_get_display_cfg_cache("discovery_prefix") + "\"><br>";
  html += "shared_device_id: <input name=\"disp_shared_device_id\" pattern=\"^[A-Za-z0-9_-]{1,64}$\" title=\"1-64 chars: letters, numbers, underscore, hyphen\" value=\"" +
          ctrl_get_display_cfg_cache("shared_device_id") + "\"><br>";
  html += "<button type=\"submit\">Save Misc</button></form></fieldset>";

  html += "<p>display_reboot_required=" + ctrl_get_display_cfg_cache("reboot_required") + "</p>";
  html += "<form method=\"post\" action=\"/reboot\"><button type=\"submit\">Reboot Controller</button></form>";
  html += "<form method=\"post\" action=\"/config\"><input type=\"hidden\" name=\"disp_reboot\" value=\"1\"><button type=\"submit\">Reboot Display (via MQTT)</button></form>";
  html += "<p>Use the display device IP + /screenshot for remote screen capture.</p>";
  html += "</body></html>";
  g_ctrl_web.send(200, "text/html", html);
}

void ctrl_ensure_web_ready() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!g_ctrl_web_started) {
    g_ctrl_web.on("/", HTTP_GET, ctrl_web_handle_root);
    g_ctrl_web.on("/config", HTTP_GET, ctrl_web_handle_config_get);
    g_ctrl_web.on("/config", HTTP_POST, ctrl_web_handle_config_post);
    g_ctrl_web.on("/reboot", HTTP_POST, ctrl_web_handle_reboot_post);
    ota_web_setup(g_ctrl_web);
    g_ctrl_web.begin();
    g_ctrl_web_started = true;
  }
  g_ctrl_web.handleClient();
}

void ctrl_publish_runtime_state() {
  if (g_controller == nullptr || !g_ctrl_mqtt.connected()) {
    return;
  }
  const auto &rt = g_controller->app().runtime();
  const bool lockout = rt.hvac_lockout();
  const auto snap = rt.snapshot();
  const char *mode = mqtt_payload::mode_to_str(snap.mode);
  const char *fan = mqtt_payload::fan_to_str(snap.fan_mode);

  char buf[32];
  g_ctrl_mqtt.publish(ctrl_topic_for("state/availability").c_str(), "online", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/lockout").c_str(), lockout ? "1" : "0", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/mode").c_str(), mode, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/fan_mode").c_str(), fan, true);
  snprintf(buf, sizeof(buf), "%.1f", rt.target_temperature_c());
  g_ctrl_mqtt.publish(ctrl_topic_for("state/target_temp_c").c_str(), buf, true);
  const auto &app = g_controller->app();
  if (app.has_indoor_temperature()) {
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(app.indoor_temperature_c()));
    g_ctrl_mqtt.publish(ctrl_topic_for("state/current_temp_c").c_str(), buf, true);
  }
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(app.indoor_humidity_pct()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/current_humidity").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%.2f", rt.filter_runtime_hours());
  g_ctrl_mqtt.publish(ctrl_topic_for("state/filter_runtime_hours").c_str(), buf, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/filter_change_required").c_str(),
                      rt.filter_runtime_hours() >= kFilterChangeThresholdHours ? "1" : "0", true);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(rt.furnace_state()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/furnace_state").c_str(), buf, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/firmware_version").c_str(),
                      THERMOSTAT_FIRMWARE_VERSION, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_ctrl_boot_count));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/boot_count").c_str(), buf, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/reset_reason").c_str(), g_ctrl_reset_reason.c_str(),
                      true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(millis() / 1000UL));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/uptime_s").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_ctrl_last_mqtt_command_ms));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/last_mqtt_command_ms").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(rt.heartbeat_last_seen_ms()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/last_espnow_rx_ms").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_controller->transport().send_ok_count()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/espnow_send_ok_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_controller->transport().send_fail_count()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/espnow_send_fail_count").c_str(), buf, true);
  if (g_ctrl_last_espnow_error != "begin_failed") {
    g_ctrl_last_espnow_error =
        g_controller->transport().send_fail_count() > 0 ? "send_failed" : "none";
  }
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(esp_get_free_heap_size()));
  g_ctrl_mqtt.publish(ctrl_topic_for("state/free_heap_bytes").c_str(), buf, true);
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
    g_ctrl_mqtt.publish(ctrl_topic_for("state/wifi_rssi").c_str(), buf, true);
  }
  g_ctrl_mqtt.publish(ctrl_topic_for("state/error_mqtt").c_str(), g_ctrl_last_mqtt_error.c_str(),
                      true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/error_ota").c_str(), g_ctrl_last_ota_error.c_str(),
                      true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/error_espnow").c_str(),
                      g_ctrl_last_espnow_error.c_str(), true);
  if (app.has_outdoor_weather()) {
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(app.outdoor_temp_c()));
    g_ctrl_mqtt.publish(ctrl_topic_for("state/outdoor_temp_c").c_str(), buf, true);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/outdoor_condition").c_str(),
                        thermostat::weather_icon_display_text(app.outdoor_icon()), true);
  }
  {
    const uint8_t *ps = app.primary_sensor_mac();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             ps[0], ps[1], ps[2], ps[3], ps[4], ps[5]);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/primary_sensor_mac").c_str(), mac_str, true);
  }
  g_ctrl_have_lockout = true;
  g_ctrl_last_lockout = lockout;
}

bool ctrl_apply_packed_command(uint32_t packed_word, bool from_mqtt) {
  if (g_controller == nullptr) {
    return false;
  }

  const thermostat::CommandApplyResult result =
      g_controller->app().on_command_word(packed_word);
  if (!result.accepted) {
    return false;
  }

  const auto snap = g_controller->app().runtime().snapshot();
  g_ctrl_shadow_mode = snap.mode;
  g_ctrl_shadow_fan = snap.fan_mode;
  g_ctrl_shadow_setpoint_c = g_controller->app().runtime().target_temperature_c();
  g_ctrl_have_shadow = true;
  if (from_mqtt) {
    g_ctrl_last_mqtt_command_ms = millis();
  }
  ctrl_publish_runtime_state();
  return true;
}

void ctrl_apply_mqtt_shadow(bool do_sync, bool do_filter_reset) {
  if (g_controller == nullptr || !g_ctrl_have_shadow) {
    return;
  }
  g_ctrl_mqtt_seq = static_cast<uint16_t>((g_ctrl_mqtt_seq + 1) & 0x1FFu);
  if (g_ctrl_mqtt_seq == 0) g_ctrl_mqtt_seq = 1;
  const CommandWord cmd = thermostat::build_command_word(
      g_ctrl_shadow_mode, g_ctrl_shadow_fan, g_ctrl_shadow_setpoint_c, g_ctrl_mqtt_seq, do_sync,
      do_filter_reset);
  ctrl_apply_packed_command(espnow_cmd::encode(cmd), true);
}

void ctrl_mqtt_on_message(char *topic, uint8_t *payload, unsigned int length) {
  if (topic == nullptr || payload == nullptr) {
    return;
  }

  char value[128];
  const size_t copy_len = (length < sizeof(value) - 1) ? length : sizeof(value) - 1;
  memcpy(value, payload, copy_len);
  value[copy_len] = '\0';

  const String topic_str(topic);
  std::string cfg_key;
  if (thermostat::management_paths::parse_cfg_set_topic(g_cfg_ctrl_mqtt_base_topic.c_str(),
                                                        topic_str.c_str(), &cfg_key)) {
    ctrl_try_update_runtime_config(cfg_key.c_str(), value);
    return;
  }
  std::string disp_cfg_key;
  if (thermostat::management_paths::parse_cfg_state_topic(
          g_cfg_display_mqtt_base_topic.c_str(), topic_str.c_str(), &disp_cfg_key)) {
    ctrl_set_display_cfg_cache(disp_cfg_key.c_str(), value);
    return;
  }
  if (topic_str == display_topic_for("state/availability")) {
    g_disp_availability = value;
    return;
  }

  // MQTT sensor intake: {base}/sensor/{mac}/temp_c or humidity
  char sensor_mac[18];
  const char *sensor_suffix = nullptr;
  if (ctrl_parse_sensor_topic(g_cfg_ctrl_mqtt_base_topic.c_str(), topic, sensor_mac,
                              &sensor_suffix)) {
    uint8_t parsed_mac[6];
    if (ctrl_parse_mac(sensor_mac, parsed_mac) && g_controller != nullptr) {
      const float fval = static_cast<float>(atof(value));
      if (strcmp(sensor_suffix, "temp_c") == 0) {
        g_controller->app().on_indoor_temperature_c(fval, parsed_mac);
      } else if (strcmp(sensor_suffix, "humidity") == 0) {
        g_controller->app().on_indoor_humidity(fval, parsed_mac);
      }
    }
    return;
  }

  if (topic_str.startsWith(ctrl_topic_for("cmd/"))) {
    g_ctrl_last_mqtt_command_ms = millis();
  }

  if (g_controller == nullptr) {
    return;
  }

  if (topic_str == ctrl_topic_for("cmd/lockout")) {
    g_controller->app().set_hvac_lockout(mqtt_payload::parse_bool(value));
    ctrl_publish_runtime_state();
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/packed_word")) {
    uint32_t packed = 0;
    if (ctrl_parse_u32_payload(value, &packed)) {
      ctrl_apply_packed_command(packed, true);
    }
    return;
  }

  // Direct controller command topics
  if (topic_str == ctrl_topic_for("cmd/mode")) {
    g_ctrl_shadow_mode = mqtt_payload::str_to_mode(value);
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/fan_mode")) {
    g_ctrl_shadow_fan = mqtt_payload::str_to_fan(value);
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/target_temp_c")) {
    g_ctrl_shadow_setpoint_c = static_cast<float>(atof(value));
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/sync") && mqtt_payload::parse_bool(value)) {
    ctrl_apply_mqtt_shadow(true, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/reboot") && mqtt_payload::parse_bool(value)) {
    ctrl_schedule_reboot();
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/reset_sequence") && mqtt_payload::parse_bool(value)) {
    g_controller->app().reset_remote_command_sequence();
    g_ctrl_mqtt_seq = 0;
    ctrl_publish_display_cmd("reset_sequence", "1");
    ctrl_publish_runtime_state();
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/filter_reset") && mqtt_payload::parse_bool(value)) {
    ctrl_apply_mqtt_shadow(false, true);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/primary_sensor_mac")) {
    ctrl_try_update_runtime_config("primary_sensor_mac", value);
    ctrl_publish_runtime_state();
    return;
  }

  // Thermostat mirrored packed command path (MQTT-primary path).
  if (topic_str == display_topic_for("state/packed_command")) {
    uint32_t packed = 0;
    if (ctrl_parse_u32_payload(value, &packed)) {
      ctrl_apply_packed_command(packed, true);
    }
    return;
  }
}

void ctrl_ensure_wifi_connected(uint32_t now_ms) {
  if (g_ctrl_wifi_reconnect_required) {
    g_ctrl_wifi_reconnect_required = false;
    WiFi.disconnect();
    g_ctrl_last_wifi_attempt_ms = 0;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if ((now_ms - g_ctrl_last_wifi_attempt_ms) < kCtrlNetworkRetryMs) {
    return;
  }
  g_ctrl_last_wifi_attempt_ms = now_ms;
  if (g_cfg_ctrl_wifi_ssid.length() == 0) {
    WiFi.begin();
  } else {
    WiFi.begin(g_cfg_ctrl_wifi_ssid.c_str(), g_cfg_ctrl_wifi_password.c_str());
  }
}

void ctrl_ensure_mqtt_connected(uint32_t now_ms) {
  if (g_cfg_ctrl_mqtt_host.length() == 0) {
    return;
  }
  if (g_ctrl_mqtt_reconfigure_required) {
    g_ctrl_mqtt_reconfigure_required = false;
    g_ctrl_mqtt_discovery_sent = false;
    g_ctrl_mqtt.disconnect();
  }
  if (WiFi.status() != WL_CONNECTED || g_ctrl_mqtt.connected()) {
    return;
  }
  if ((now_ms - g_ctrl_last_mqtt_attempt_ms) < kCtrlNetworkRetryMs) {
    return;
  }
  g_ctrl_last_mqtt_attempt_ms = now_ms;
  g_ctrl_mqtt.setServer(g_cfg_ctrl_mqtt_host.c_str(), g_cfg_ctrl_mqtt_port);

  bool ok = false;
  if (g_cfg_ctrl_mqtt_user.length() == 0) {
    ok = g_ctrl_mqtt.connect(g_cfg_ctrl_mqtt_client_id.c_str());
  } else {
    ok = g_ctrl_mqtt.connect(g_cfg_ctrl_mqtt_client_id.c_str(), g_cfg_ctrl_mqtt_user.c_str(),
                             g_cfg_ctrl_mqtt_password.c_str());
  }
  if (!ok) {
    g_ctrl_last_mqtt_error = String("connect_state_") + String(g_ctrl_mqtt.state());
    return;
  }
  g_ctrl_last_mqtt_error = "none";

  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/lockout").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/mode").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/fan_mode").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/target_temp_c").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/packed_word").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/sync").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/reboot").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/reset_sequence").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/filter_reset").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/primary_sensor_mac").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cfg/+/set").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("sensor/+/temp_c").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("sensor/+/humidity").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/packed_command").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/availability").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("cfg/+/state").c_str());
  ctrl_publish_discovery();
  ctrl_publish_all_cfg_state();
  ctrl_publish_runtime_state();
}

void ctrl_ensure_ota_ready() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!g_ctrl_ota_started) {
    ArduinoOTA.setHostname(g_cfg_ctrl_ota_hostname.c_str());
    if (g_cfg_ctrl_ota_password.length() > 0) {
      ArduinoOTA.setPassword(g_cfg_ctrl_ota_password.c_str());
    }
    ArduinoOTA.onError([](ota_error_t error) {
      g_ctrl_last_ota_error = String("ota_error_") + String(static_cast<unsigned>(error));
    });
    ArduinoOTA.onEnd([]() { g_ctrl_last_ota_error = "none"; });
    ArduinoOTA.begin();
    g_ctrl_ota_started = true;
  }
  ArduinoOTA.handle();
}

void ctrl_ensure_mdns_ready() {
  if (WiFi.status() != WL_CONNECTED || g_ctrl_mdns_started) {
    return;
  }
  const char *host = g_cfg_ctrl_ota_hostname.length() > 0 ? g_cfg_ctrl_ota_hostname.c_str()
                                                           : "furnace-controller";
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    g_ctrl_mdns_started = true;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  g_ctrl_cfg_ready = g_ctrl_cfg.begin("cfg_ctrl", false);
  ctrl_load_runtime_config();
  g_ctrl_boot_count =
      g_ctrl_cfg_ready ? (g_ctrl_cfg.getUInt("boot_cnt", 0U) + 1U) : 0U;
  if (g_ctrl_cfg_ready) {
    g_ctrl_cfg.putUInt("boot_cnt", g_ctrl_boot_count);
  }
  g_ctrl_reset_reason = ctrl_reset_reason_text(esp_reset_reason());

  thermostat::ControllerConfig controller_cfg;
  controller_cfg.failsafe_timeout_ms = 300000;
  controller_cfg.fan_circulate_period_min = 60;
  controller_cfg.fan_circulate_duration_min = 10;

  thermostat::EspNowControllerConfig transport_cfg;
  transport_cfg.channel = g_cfg_ctrl_espnow_channel;
  transport_cfg.heartbeat_interval_ms = 10000;
  transport_cfg.peer_count = 0;

  // Parse comma-separated peer MACs (new multi-peer config)
  if (g_cfg_ctrl_espnow_peer_macs.length() > 0) {
    String macs = g_cfg_ctrl_espnow_peer_macs;
    while (macs.length() > 0 && transport_cfg.peer_count < thermostat::kMaxEspNowPeers) {
      int comma = macs.indexOf(',');
      String one_mac;
      if (comma >= 0) {
        one_mac = macs.substring(0, comma);
        macs = macs.substring(comma + 1);
      } else {
        one_mac = macs;
        macs = "";
      }
      one_mac.trim();
      if (one_mac.length() >= 17) {
        uint8_t parsed[6];
        if (ctrl_parse_mac(one_mac.c_str(), parsed)) {
          memcpy(transport_cfg.peer_macs[transport_cfg.peer_count], parsed, 6);
          ++transport_cfg.peer_count;
        }
      }
    }
  }

  // Fall back to single peer_mac for backward compatibility
  if (transport_cfg.peer_count == 0) {
    uint8_t single_peer[6];
    if (ctrl_parse_mac(g_cfg_ctrl_espnow_peer_mac.c_str(), single_peer)) {
      memcpy(transport_cfg.peer_macs[0], single_peer, 6);
      transport_cfg.peer_count = 1;
    }
  }

  uint8_t lmk[16] = {0};
  if (ctrl_parse_lmk_hex(g_cfg_ctrl_espnow_lmk.c_str(), lmk) &&
      transport_cfg.peer_count > 0) {
    // Only enable encryption if we have non-broadcast peers
    bool has_unicast = false;
    for (int i = 0; i < transport_cfg.peer_count; ++i) {
      if (!ctrl_is_broadcast_mac(transport_cfg.peer_macs[i])) {
        has_unicast = true;
        break;
      }
    }
    if (has_unicast) {
      memcpy(transport_cfg.lmk, lmk, sizeof(lmk));
      transport_cfg.encrypted = true;
    }
  }

  static thermostat::ControllerNode node(controller_cfg, transport_cfg);
  g_controller = &node;
  WiFi.mode(WIFI_STA);
  g_ctrl_mqtt.setServer(g_cfg_ctrl_mqtt_host.c_str(), g_cfg_ctrl_mqtt_port);
  g_ctrl_mqtt.setCallback(ctrl_mqtt_on_message);

  const bool ok = g_controller->begin();
  g_ctrl_last_espnow_error = ok ? "none" : "begin_failed";

  // Apply persisted primary sensor MAC
  {
    uint8_t ps_mac[6];
    if (ctrl_parse_mac(g_cfg_ctrl_primary_sensor_mac.c_str(), ps_mac)) {
      g_controller->app().set_primary_sensor_mac(ps_mac);
    }
  }

  g_relay_io.begin();
  Serial.printf("controller_node_begin=%u\n", static_cast<unsigned>(ok));
  ota_rollback_begin();
}

void loop() {
  const uint32_t now = millis();
  if (g_ctrl_reboot_requested && static_cast<int32_t>(now - g_ctrl_reboot_at_ms) >= 0) {
    ESP.restart();
  }
  if (g_controller != nullptr) {
    g_controller->tick(now);
    const ThermostatSnapshot snap = g_controller->app().runtime().snapshot();
    g_relay_io.apply(now, snap.relay, snap.failsafe_active || snap.hvac_lockout);
    ctrl_ensure_wifi_connected(now);
    ctrl_ensure_mdns_ready();
    ctrl_ensure_web_ready();
    ctrl_ensure_ota_ready();
    ctrl_ensure_mqtt_connected(now);
    g_ctrl_mqtt.loop();
    const bool mqtt_primary_active =
        g_ctrl_mqtt.connected() &&
        (static_cast<uint32_t>(now - g_ctrl_last_mqtt_command_ms) < kCtrlMqttPrimaryHoldMs);
    g_controller->set_espnow_command_enabled(!mqtt_primary_active);
    ctrl_poll_weather(now);
    if (g_ctrl_mqtt.connected()) {
      if (!g_ctrl_have_lockout || g_ctrl_last_lockout != snap.hvac_lockout ||
          (now - g_ctrl_last_mqtt_publish_ms) >= kCtrlMqttPublishMs) {
        g_ctrl_last_mqtt_publish_ms = now;
        ctrl_publish_runtime_state();
      }
    }
  }
  ota_rollback_check(WiFi.status() == WL_CONNECTED && g_ctrl_mqtt.connected());
  delay(100);
}
#endif
