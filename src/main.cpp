#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "controller_app.h"
#include "controller_relay_io.h"
#include "controller_node.h"
#include "esp32s3_thermostat_firmware.h"
#include "espnow_cmd_word.h"
#include "management_paths.h"
#include "thermostat_device_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <esp_system.h>
#endif

namespace {

class DemoTransport final : public thermostat::IControllerTransport {
 public:
  void publish_telemetry(const thermostat::ControllerTelemetry &telemetry) override {
    last = telemetry;
    ++publish_count;
  }

  thermostat::ControllerTelemetry last{};
  uint32_t publish_count = 0;
};

}  // namespace

#if defined(ARDUINO)

#if defined(THERMOSTAT_ROLE_THERMOSTAT)
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  thermostat::thermostat_firmware_setup();
}

void loop() {
  thermostat::thermostat_firmware_loop();
  delay(5);
}

#else
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

constexpr uint32_t kCtrlNetworkRetryMs = 5000;
constexpr uint32_t kCtrlMqttPublishMs = 10000;
constexpr uint32_t kCtrlMqttPrimaryHoldMs = 30000;

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
#define THERMOSTAT_CONTROLLER_ESPNOW_PEER_MAC ""
#endif

#ifndef THERMOSTAT_CONTROLLER_ESPNOW_LMK
#define THERMOSTAT_CONTROLLER_ESPNOW_LMK ""
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
String g_cfg_ctrl_espnow_lmk = THERMOSTAT_CONTROLLER_ESPNOW_LMK;
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
    {"outdoor_temp_topic", "unknown"},
    {"weather_condition_topic", "unknown"},
    {"display_timeout_s", "unknown"},
    {"temp_comp_c", "unknown"},
    {"temperature_unit", "unknown"},
    {"ota_hostname", "unknown"},
    {"ota_password", "unknown"},
    {"espnow_channel", "unknown"},
    {"espnow_peer_mac", "unknown"},
    {"espnow_lmk", "unknown"},
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
  g_cfg_ctrl_espnow_lmk = g_ctrl_cfg.getString("esp_lmk", g_cfg_ctrl_espnow_lmk);
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

void ctrl_publish_cfg_value(const char *key, const String &value, bool redact) {
  if (!g_ctrl_mqtt.connected()) {
    return;
  }
  String topic = ctrl_topic_for("cfg");
  topic += "/";
  topic += key;
  topic += "/state";
  const char *payload = value.c_str();
  if (redact) {
    payload = value.length() > 0 ? "set" : "unset";
  }
  g_ctrl_mqtt.publish(topic.c_str(), payload, true);
}

void ctrl_publish_all_cfg_state() {
  ctrl_publish_cfg_value("wifi_ssid", g_cfg_ctrl_wifi_ssid, false);
  ctrl_publish_cfg_value("wifi_password", g_cfg_ctrl_wifi_password, true);
  ctrl_publish_cfg_value("mqtt_host", g_cfg_ctrl_mqtt_host, false);
  ctrl_publish_cfg_value("mqtt_port", String(g_cfg_ctrl_mqtt_port), false);
  ctrl_publish_cfg_value("mqtt_user", g_cfg_ctrl_mqtt_user, false);
  ctrl_publish_cfg_value("mqtt_password", g_cfg_ctrl_mqtt_password, true);
  ctrl_publish_cfg_value("mqtt_client_id", g_cfg_ctrl_mqtt_client_id, false);
  ctrl_publish_cfg_value("mqtt_base_topic", g_cfg_ctrl_mqtt_base_topic, false);
  ctrl_publish_cfg_value("display_mqtt_base_topic", g_cfg_display_mqtt_base_topic, false);
  ctrl_publish_cfg_value("shared_device_id", g_cfg_shared_device_id, false);
  ctrl_publish_cfg_value("ota_hostname", g_cfg_ctrl_ota_hostname, false);
  ctrl_publish_cfg_value("ota_password", g_cfg_ctrl_ota_password, true);
  ctrl_publish_cfg_value("espnow_channel", String(g_cfg_ctrl_espnow_channel), false);
  ctrl_publish_cfg_value("espnow_peer_mac", g_cfg_ctrl_espnow_peer_mac, false);
  ctrl_publish_cfg_value("espnow_lmk", g_cfg_ctrl_espnow_lmk, true);
  ctrl_publish_cfg_value("reboot_required", g_ctrl_cfg_reboot_required ? "1" : "0", false);
}

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
  } else if (key == "espnow_lmk") {
    g_cfg_ctrl_espnow_lmk = value;
    g_ctrl_cfg.putString("esp_lmk", value);
    g_ctrl_cfg_reboot_required = true;
  } else {
    known = false;
  }

  if (!known) {
    return false;
  }
  if (g_ctrl_mqtt.connected()) {
    ctrl_publish_cfg_value(key.c_str(), value,
                           key == "wifi_password" || key == "mqtt_password" ||
                               key == "ota_password" || key == "espnow_lmk");
  }
  return true;
}

bool ctrl_parse_bool_payload(const char *value) {
  if (value == nullptr) {
    return false;
  }
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
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

  char payload[768];
  snprintf(payload, sizeof(payload),
           "{\"name\":\"HVAC Lockout\",\"uniq_id\":\"%s_lockout\",\"cmd_t\":\"%s/cmd/lockout\","
           "\"stat_t\":\"%s/state/lockout\",\"pl_on\":\"1\",\"pl_off\":\"0\","
           "\"dev\":{\"ids\":[\"%s\"],\"name\":\"Wireless Thermostat System\","
           "\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
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
  body += "\"espnow_lmk\":\"" + String(g_cfg_ctrl_espnow_lmk.length() > 0 ? "set" : "unset") + "\",";
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

void ctrl_web_handle_root() {
  String html;
  html.reserve(4096);
  html += "<html><body><h1>System Config (Controller + Display)</h1>";
  html += "<p><a href=\"/config\">JSON config</a></p>";
  html += "<h2>Controller</h2>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"wifi_ssid\" value=\"" + g_cfg_ctrl_wifi_ssid + "\"><br>";
  html += "wifi_password: <input name=\"wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"mqtt_host\" value=\"" + g_cfg_ctrl_mqtt_host + "\"><br>";
  html += "mqtt_port: <input name=\"mqtt_port\" value=\"" + String(g_cfg_ctrl_mqtt_port) + "\"><br>";
  html += "mqtt_user: <input name=\"mqtt_user\" value=\"" + g_cfg_ctrl_mqtt_user + "\"><br>";
  html += "mqtt_password: <input name=\"mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"mqtt_client_id\" value=\"" + g_cfg_ctrl_mqtt_client_id + "\"><br>";
  html += "mqtt_base_topic: <input name=\"mqtt_base_topic\" value=\"" + g_cfg_ctrl_mqtt_base_topic + "\"><br>";
  html += "display_mqtt_base_topic: <input name=\"display_mqtt_base_topic\" value=\"" + g_cfg_display_mqtt_base_topic + "\"><br>";
  html += "shared_device_id: <input name=\"shared_device_id\" value=\"" + g_cfg_shared_device_id + "\"><br>";
  html += "ota_hostname: <input name=\"ota_hostname\" value=\"" + g_cfg_ctrl_ota_hostname + "\"><br>";
  html += "ota_password: <input name=\"ota_password\" value=\"\"><br>";
  html += "espnow_channel: <input name=\"espnow_channel\" value=\"" + String(g_cfg_ctrl_espnow_channel) + "\"><br>";
  html += "espnow_peer_mac: <input name=\"espnow_peer_mac\" value=\"" + g_cfg_ctrl_espnow_peer_mac + "\"><br>";
  html += "espnow_lmk: <input name=\"espnow_lmk\" value=\"\"><br>";
  html += "<button type=\"submit\">Save Controller</button></form>";
  html += "<p>reboot_required=" + String(g_ctrl_cfg_reboot_required ? "true" : "false") + "</p>";

  html += "<h2>Display (via MQTT)</h2>";
  html += "<p>availability=" + g_disp_availability + "</p>";
  html += "<form method=\"post\" action=\"/config\">";
  html += "wifi_ssid: <input name=\"disp_wifi_ssid\" value=\"" + ctrl_get_display_cfg_cache("wifi_ssid") + "\"><br>";
  html += "wifi_password: <input name=\"disp_wifi_password\" value=\"\"><br>";
  html += "mqtt_host: <input name=\"disp_mqtt_host\" value=\"" + ctrl_get_display_cfg_cache("mqtt_host") + "\"><br>";
  html += "mqtt_port: <input name=\"disp_mqtt_port\" value=\"" + ctrl_get_display_cfg_cache("mqtt_port") + "\"><br>";
  html += "mqtt_user: <input name=\"disp_mqtt_user\" value=\"" + ctrl_get_display_cfg_cache("mqtt_user") + "\"><br>";
  html += "mqtt_password: <input name=\"disp_mqtt_password\" value=\"\"><br>";
  html += "mqtt_client_id: <input name=\"disp_mqtt_client_id\" value=\"" + ctrl_get_display_cfg_cache("mqtt_client_id") + "\"><br>";
  html += "mqtt_base_topic: <input name=\"disp_mqtt_base_topic\" value=\"" + ctrl_get_display_cfg_cache("mqtt_base_topic") + "\"><br>";
  html += "discovery_prefix: <input name=\"disp_discovery_prefix\" value=\"" + ctrl_get_display_cfg_cache("discovery_prefix") + "\"><br>";
  html += "shared_device_id: <input name=\"disp_shared_device_id\" value=\"" + ctrl_get_display_cfg_cache("shared_device_id") + "\"><br>";
  html += "display_timeout_s: <input name=\"disp_display_timeout_s\" value=\"" + ctrl_get_display_cfg_cache("display_timeout_s") + "\"><br>";
  html += "temp_comp_c: <input name=\"disp_temp_comp_c\" value=\"" + ctrl_get_display_cfg_cache("temp_comp_c") + "\"><br>";
  html += "temperature_unit: <input name=\"disp_temperature_unit\" value=\"" + ctrl_get_display_cfg_cache("temperature_unit") + "\"><br>";
  html += "outdoor_temp_topic: <input name=\"disp_outdoor_temp_topic\" value=\"" + ctrl_get_display_cfg_cache("outdoor_temp_topic") + "\"><br>";
  html += "weather_condition_topic: <input name=\"disp_weather_condition_topic\" value=\"" + ctrl_get_display_cfg_cache("weather_condition_topic") + "\"><br>";
  html += "ota_hostname: <input name=\"disp_ota_hostname\" value=\"" + ctrl_get_display_cfg_cache("ota_hostname") + "\"><br>";
  html += "ota_password: <input name=\"disp_ota_password\" value=\"\"><br>";
  html += "espnow_channel: <input name=\"disp_espnow_channel\" value=\"" + ctrl_get_display_cfg_cache("espnow_channel") + "\"><br>";
  html += "espnow_peer_mac: <input name=\"disp_espnow_peer_mac\" value=\"" + ctrl_get_display_cfg_cache("espnow_peer_mac") + "\"><br>";
  html += "espnow_lmk: <input name=\"disp_espnow_lmk\" value=\"\"><br>";
  html += "controller_timeout_ms: <input name=\"disp_controller_timeout_ms\" value=\"" + ctrl_get_display_cfg_cache("controller_timeout_ms") + "\"><br>";
  html += "<button type=\"submit\">Save Display</button></form>";
  html += "<p>display_reboot_required=" + ctrl_get_display_cfg_cache("reboot_required") + "</p>";
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
  const char *mode = "off";
  if (snap.mode == FurnaceMode::Heat) mode = "heat";
  if (snap.mode == FurnaceMode::Cool) mode = "cool";
  const char *fan = "auto";
  if (snap.fan_mode == FanMode::AlwaysOn) fan = "on";
  if (snap.fan_mode == FanMode::Circulate) fan = "circulate";

  char buf[32];
  g_ctrl_mqtt.publish(ctrl_topic_for("state/availability").c_str(), "online", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/lockout").c_str(), lockout ? "1" : "0", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/mode").c_str(), mode, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/fan_mode").c_str(), fan, true);
  snprintf(buf, sizeof(buf), "%.1f", rt.target_temperature_c());
  g_ctrl_mqtt.publish(ctrl_topic_for("state/target_temp_c").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%.2f", rt.filter_runtime_hours());
  g_ctrl_mqtt.publish(ctrl_topic_for("state/filter_runtime_hours").c_str(), buf, true);
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
  g_ctrl_have_lockout = true;
  g_ctrl_last_lockout = lockout;
}

void ctrl_apply_mqtt_shadow(bool do_sync, bool do_filter_reset) {
  if (g_controller == nullptr || !g_ctrl_have_shadow) {
    return;
  }
  CommandWord cmd;
  cmd.mode = g_ctrl_shadow_mode;
  cmd.fan = g_ctrl_shadow_fan;
  int setpoint_decic = static_cast<int>(g_ctrl_shadow_setpoint_c * 10.0f + 0.5f);
  if (setpoint_decic < 0) setpoint_decic = 0;
  if (setpoint_decic > 400) setpoint_decic = 400;
  cmd.setpoint_decic = static_cast<uint16_t>(setpoint_decic);
  g_ctrl_mqtt_seq = static_cast<uint16_t>((g_ctrl_mqtt_seq + 1) & 0x1FFu);
  if (g_ctrl_mqtt_seq == 0) g_ctrl_mqtt_seq = 1;
  cmd.seq = g_ctrl_mqtt_seq;
  cmd.sync_request = do_sync;
  cmd.filter_reset = do_filter_reset;
  g_controller->app().on_command_word(espnow_cmd::encode(cmd));
  g_ctrl_last_mqtt_command_ms = millis();
  ctrl_publish_runtime_state();
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

  if (g_controller == nullptr) {
    return;
  }

  if (topic_str == ctrl_topic_for("cmd/lockout")) {
    g_controller->app().set_hvac_lockout(ctrl_parse_bool_payload(value));
    ctrl_publish_runtime_state();
    return;
  }

  // Direct controller command topics
  if (topic_str == ctrl_topic_for("cmd/mode")) {
    if (strcmp(value, "heat") == 0) g_ctrl_shadow_mode = FurnaceMode::Heat;
    else if (strcmp(value, "cool") == 0) g_ctrl_shadow_mode = FurnaceMode::Cool;
    else g_ctrl_shadow_mode = FurnaceMode::Off;
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/fan_mode")) {
    if (strcmp(value, "on") == 0 || strcmp(value, "always on") == 0) g_ctrl_shadow_fan = FanMode::AlwaysOn;
    else if (strcmp(value, "circulate") == 0) g_ctrl_shadow_fan = FanMode::Circulate;
    else g_ctrl_shadow_fan = FanMode::Automatic;
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
  if (topic_str == ctrl_topic_for("cmd/sync") && ctrl_parse_bool_payload(value)) {
    ctrl_apply_mqtt_shadow(true, false);
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/filter_reset") && ctrl_parse_bool_payload(value)) {
    ctrl_apply_mqtt_shadow(false, true);
    return;
  }

  // Thermostat state mirror topics (MQTT-primary path)
  if (topic_str == display_topic_for("state/mode")) {
    if (strcmp(value, "heat") == 0) g_ctrl_shadow_mode = FurnaceMode::Heat;
    else if (strcmp(value, "cool") == 0) g_ctrl_shadow_mode = FurnaceMode::Cool;
    else g_ctrl_shadow_mode = FurnaceMode::Off;
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == display_topic_for("state/fan_mode")) {
    if (strcmp(value, "on") == 0 || strcmp(value, "always on") == 0) g_ctrl_shadow_fan = FanMode::AlwaysOn;
    else if (strcmp(value, "circulate") == 0) g_ctrl_shadow_fan = FanMode::Circulate;
    else g_ctrl_shadow_fan = FanMode::Automatic;
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
    return;
  }
  if (topic_str == display_topic_for("state/target_temp_c")) {
    g_ctrl_shadow_setpoint_c = static_cast<float>(atof(value));
    g_ctrl_have_shadow = true;
    ctrl_apply_mqtt_shadow(false, false);
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
    return;
  }

  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/lockout").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/mode").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/fan_mode").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/target_temp_c").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/sync").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cmd/filter_reset").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("cfg/+/set").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/mode").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/fan_mode").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/target_temp_c").c_str());
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
  if (ctrl_parse_mac(g_cfg_ctrl_espnow_peer_mac.c_str(), transport_cfg.peer_mac)) {
    uint8_t lmk[16] = {0};
    if (ctrl_parse_lmk_hex(g_cfg_ctrl_espnow_lmk.c_str(), lmk)) {
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
  g_relay_io.begin();
  Serial.printf("controller_node_begin=%u\n", static_cast<unsigned>(ok));
}

void loop() {
  const uint32_t now = millis();
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
    if (g_ctrl_mqtt.connected()) {
      if (!g_ctrl_have_lockout || g_ctrl_last_lockout != snap.hvac_lockout ||
          (now - g_ctrl_last_mqtt_publish_ms) >= kCtrlMqttPublishMs) {
        g_ctrl_last_mqtt_publish_ms = now;
        ctrl_publish_runtime_state();
      }
    }
  }
  delay(100);
}
#endif

#else

int run_smoke_checks() {
  DemoTransport transport;

  thermostat::ControllerConfig cfg;
  cfg.failsafe_timeout_ms = 5000;
  cfg.fan_circulate_period_min = 5;
  cfg.fan_circulate_duration_min = 2;

  thermostat::ControllerApp app(transport, cfg);

  CommandWord first;
  first.mode = FurnaceMode::Cool;
  first.fan = FanMode::Automatic;
  first.setpoint_decic = 230;
  first.seq = 10;

  const auto first_result = app.on_command_word(espnow_cmd::encode(first));
  if (!first_result.accepted) {
    return 1;
  }

  const auto duplicate_result = app.on_command_word(espnow_cmd::encode(first));
  if (!duplicate_result.stale_or_duplicate) {
    return 2;
  }

  app.on_indoor_temperature_c(25.0f);
  app.on_heartbeat(1000);
  app.tick(2000);
  if (!app.runtime().cool_demand() || app.runtime().heat_demand() ||
      app.runtime().fan_demand()) {
    return 3;
  }

  app.tick(8000);
  if (!app.runtime().failsafe_active() || app.runtime().cool_demand() ||
      app.runtime().heat_demand() || app.runtime().fan_demand()) {
    return 4;
  }

  app.on_heartbeat(9000);
  app.tick(9001);

  CommandWord second;
  second.mode = FurnaceMode::Off;
  second.fan = FanMode::AlwaysOn;
  second.setpoint_decic = 210;
  second.seq = 11;
  const auto second_result = app.on_command_word(espnow_cmd::encode(second));
  if (!second_result.accepted) {
    return 5;
  }

  app.tick(9100);
  if (!app.runtime().fan_demand()) {
    return 6;
  }

  if (transport.publish_count == 0) {
    return 7;
  }

  return 0;
}

int main() { return run_smoke_checks(); }
#endif
