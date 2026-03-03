#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>

#include "controller/controller_relay_io.h"
#include "controller/controller_node.h"
#include "controller/pirateweather.h"
#include "weather_icon.h"
#include "command_builder.h"
#include "espnow_cmd_word.h"
#include "management_paths.h"
#include "mqtt_payload.h"
#include "device_registry.h"
#include "wifi_watchdog.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include "improv_ble_provisioning.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <esp_mac.h>
#include <esp_system.h>
#include "ota_web_updater.h"
#include "controller/audit_log.h"
#include "web/web_ui_escape.h"
#include "web/web_ui_shell.h"
#include "web/web_ui_fields.h"

thermostat::ControllerNode *g_controller = nullptr;
thermostat::ControllerRelayIo g_relay_io;
thermostat::AuditLog g_audit_log;
WiFiClient g_ctrl_wifi_client;
PubSubClient g_ctrl_mqtt(g_ctrl_wifi_client);
WebServer g_ctrl_web(80);
uint32_t g_ctrl_last_wifi_attempt_ms = 0;
uint32_t g_ctrl_last_mqtt_attempt_ms = 0;
uint32_t g_ctrl_last_mqtt_publish_ms = 0;
bool g_ctrl_last_lockout = false;
bool g_ctrl_have_lockout = false;
uint32_t g_ctrl_last_mqtt_command_ms = 0;
bool g_cfg_ctrl_allow_ha = true;       // NVS "allow_ha"
bool g_cfg_ctrl_mqtt_enabled = true;   // NVS "mqtt_en"
bool g_cfg_ctrl_espnow_enabled = true; // NVS "espnow_en"
String g_ctrl_last_mqtt_error = "none";
String g_ctrl_last_ota_error = "none";
String g_ctrl_last_espnow_error = "none";
uint16_t g_ctrl_mqtt_seq = 0;
bool g_ctrl_mqtt_discovery_sent = false;
bool g_ctrl_have_shadow = false;
FurnaceMode g_ctrl_shadow_mode = FurnaceMode::Off;
FanMode g_ctrl_shadow_fan = FanMode::Automatic;
float g_ctrl_shadow_setpoint_c = 20.0f;
// Track last-persisted values to skip NVS writes when nothing changed
FurnaceMode g_ctrl_persisted_mode = FurnaceMode::Off;
FanMode g_ctrl_persisted_fan = FanMode::Automatic;
float g_ctrl_persisted_setpoint_c = -999.0f;  // sentinel: force write on first call
bool g_ctrl_ota_started = false;
bool g_ctrl_web_started = false;
DeviceRegistry g_device_registry;
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
bool g_ctrl_wifi_provisioning_started = false;
bool g_ctrl_wifi_has_attempted_stored_connect = false;
uint32_t g_ctrl_first_wifi_attempt_ms = 0;
constexpr uint32_t kCtrlProvisionStartDelayMs = 15000;

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

#ifndef THERMOSTAT_MQTT_UNIQUE_DEVICE_ID
#define THERMOSTAT_MQTT_UNIQUE_DEVICE_ID "wireless_thermostat_system"
#endif

#ifndef THERMOSTAT_MQTT_DISCOVERY_PREFIX
#define THERMOSTAT_MQTT_DISCOVERY_PREFIX "homeassistant"
#endif

// Device discovery topic uses the compile-time base (not MAC-suffixed runtime
// value) so all devices in the system share the same discovery namespace.
#define THERMOSTAT_DEVICE_DISCOVERY_PREFIX THERMOSTAT_MQTT_UNIQUE_DEVICE_ID "/devices"

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
String g_cfg_unique_device_id = THERMOSTAT_MQTT_UNIQUE_DEVICE_ID;
String g_cfg_ctrl_discovery_prefix = THERMOSTAT_MQTT_DISCOVERY_PREFIX;
String g_cfg_ctrl_ota_hostname = THERMOSTAT_CONTROLLER_OTA_HOSTNAME;
String g_cfg_ctrl_ota_password = THERMOSTAT_CONTROLLER_OTA_PASSWORD;
uint8_t g_cfg_ctrl_espnow_channel = THERMOSTAT_CONTROLLER_ESPNOW_CHANNEL;
String g_cfg_ctrl_espnow_lmk = THERMOSTAT_CONTROLLER_ESPNOW_LMK;
String g_cfg_ctrl_devices = "";  // NVS "devices": semicolon-separated "MAC[=role]"
String g_cfg_ctrl_pirateweather_api_key = THERMOSTAT_CONTROLLER_PIRATEWEATHER_API_KEY;
String g_cfg_ctrl_pirateweather_zip = THERMOSTAT_CONTROLLER_PIRATEWEATHER_ZIP;
bool g_ctrl_mqtt_reconfigure_required = false;
bool g_ctrl_wifi_reconnect_required = false;
bool g_ctrl_cfg_reboot_required = false;
bool g_ctrl_temp_unit_f = false;
uint16_t g_cfg_fan_circ_period_min = 60;
uint16_t g_cfg_fan_circ_duration_min = 10;
String g_disp_availability = "unknown";


// ── Device list helpers ────────────────────────────────────────────────────────

// Parse devices string ("MAC[=role];...") and populate transport peer_macs.
// Returns peer count populated.
static int ctrl_collect_peer_macs(const String &devices,
                                   uint8_t peer_macs[][6], int max_peers,
                                   bool (*parse_mac_fn)(const char *, uint8_t[6])) {
  int count = 0;
  String remaining = devices;
  while (remaining.length() > 0 && count < max_peers) {
    int semi = remaining.indexOf(';');
    String entry = (semi >= 0) ? remaining.substring(0, semi) : remaining;
    remaining = (semi >= 0) ? remaining.substring(semi + 1) : "";
    entry.trim();
    if (entry.length() == 0) continue;
    int eq = entry.indexOf('=');
    String mac_str = (eq >= 0) ? entry.substring(0, eq) : entry;
    mac_str.trim();
    if (mac_str.length() >= 17) {
      if (parse_mac_fn(mac_str.c_str(), peer_macs[count])) {
        ++count;
      }
    }
  }
  return count;
}

// Find the MAC with role="temp" in the devices string. Returns "" if none.
static String ctrl_find_temp_sensor_mac_str(const String &devices) {
  String remaining = devices;
  while (remaining.length() > 0) {
    int semi = remaining.indexOf(';');
    String entry = (semi >= 0) ? remaining.substring(0, semi) : remaining;
    remaining = (semi >= 0) ? remaining.substring(semi + 1) : "";
    entry.trim();
    int eq = entry.indexOf('=');
    if (eq >= 0) {
      String mac = entry.substring(0, eq);
      String role = entry.substring(eq + 1);
      mac.trim(); role.trim();
      if (role == "temp") return mac;
    }
  }
  return "";
}

// Returns lowercase last 3 bytes of base MAC as hex, e.g. "ddeeff".
// Uses esp_efuse_mac_get_default() which works without WiFi init.
static String mac_suffix() {
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return String("000000");
  char buf[7];
  snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  return String(buf);
}

void ctrl_load_runtime_config() {
  if (!g_ctrl_cfg_ready) {
    return;
  }

  // Compute MAC-based suffix and apply to defaults for uniqueness.
  // If a user has saved a custom value, getString() returns it instead.
  String suffix = mac_suffix();
  g_cfg_unique_device_id = String(THERMOSTAT_MQTT_UNIQUE_DEVICE_ID) + "_" + suffix;
  g_cfg_ctrl_mqtt_client_id = String(THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID) + "-" + suffix;
  g_cfg_ctrl_ota_hostname = String(THERMOSTAT_CONTROLLER_OTA_HOSTNAME) + "-" + suffix;

  // One-time migration: clear old defaults so new MAC-suffixed defaults take effect.
  // Matches both the original non-suffixed default and the broken _000000 fallback.
  auto is_stale_default = [](const String &stored, const char *base, const char *sep) {
    return stored == base ||
           stored == (String(base) + sep + "000000");
  };
  if (is_stale_default(g_ctrl_cfg.getString("shared_id", ""), THERMOSTAT_MQTT_UNIQUE_DEVICE_ID, "_")) {
    g_ctrl_cfg.remove("shared_id");
  }
  if (is_stale_default(g_ctrl_cfg.getString("mqtt_cid", ""), THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID, "-")) {
    g_ctrl_cfg.remove("mqtt_cid");
  }
  if (is_stale_default(g_ctrl_cfg.getString("ota_host", ""), THERMOSTAT_CONTROLLER_OTA_HOSTNAME, "-")) {
    g_ctrl_cfg.remove("ota_host");
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
  g_cfg_ctrl_discovery_prefix = g_ctrl_cfg.getString("disc_pref", g_cfg_ctrl_discovery_prefix);
  g_cfg_unique_device_id = g_ctrl_cfg.getString("shared_id", g_cfg_unique_device_id);
  g_cfg_ctrl_ota_hostname = g_ctrl_cfg.getString("ota_host", g_cfg_ctrl_ota_hostname);
  g_cfg_ctrl_ota_password = g_ctrl_cfg.getString("ota_pwd", g_cfg_ctrl_ota_password);
  g_cfg_ctrl_espnow_channel = static_cast<uint8_t>(g_ctrl_cfg.getUChar("esp_ch", g_cfg_ctrl_espnow_channel));
  g_cfg_ctrl_espnow_lmk = g_ctrl_cfg.getString("esp_lmk", g_cfg_ctrl_espnow_lmk);
  g_cfg_ctrl_devices = g_ctrl_cfg.getString("devices", "");
  g_cfg_ctrl_allow_ha = g_ctrl_cfg.getBool("allow_ha", true);
  g_cfg_ctrl_mqtt_enabled = g_ctrl_cfg.getBool("mqtt_en", true);
  g_cfg_ctrl_espnow_enabled = g_ctrl_cfg.getBool("espnow_en", true);

  // Migration: build unified devices list from old NVS keys on first boot
  if (g_cfg_ctrl_devices.length() == 0) {
    String legacy_primary = g_ctrl_cfg.getString("pri_sensor", "");
    String legacy_peer = g_ctrl_cfg.getString("esp_peer", "");
    String legacy_peers = g_ctrl_cfg.getString("esp_peers", "");
    String migrated = "";
    auto append_if_new = [&](const String &mac, const char *role) {
      if (mac.length() < 17 || mac == "FF:FF:FF:FF:FF:FF") return;
      if (migrated.indexOf(mac) >= 0) return;
      if (migrated.length() > 0) migrated += ";";
      migrated += mac;
      if (role && role[0]) { migrated += "="; migrated += role; }
    };
    append_if_new(legacy_primary, "temp");
    append_if_new(legacy_peer, "");
    String peers = legacy_peers;
    while (peers.length() > 0) {
      int comma = peers.indexOf(',');
      String one = (comma >= 0) ? peers.substring(0, comma) : peers;
      peers = (comma >= 0) ? peers.substring(comma + 1) : "";
      one.trim();
      append_if_new(one, "");
    }
    if (migrated.length() > 0) {
      g_cfg_ctrl_devices = migrated;
      g_ctrl_cfg.putString("devices", migrated);
    }
  }
  g_cfg_ctrl_pirateweather_api_key = g_ctrl_cfg.getString("pw_key", g_cfg_ctrl_pirateweather_api_key);
  g_cfg_ctrl_pirateweather_zip = g_ctrl_cfg.getString("pw_zip", g_cfg_ctrl_pirateweather_zip);
  g_ctrl_temp_unit_f = g_ctrl_cfg.getBool("temp_u_f", g_ctrl_temp_unit_f);
  g_cfg_fan_circ_period_min = static_cast<uint16_t>(g_ctrl_cfg.getUInt("fan_circ_per", g_cfg_fan_circ_period_min));
  g_cfg_fan_circ_duration_min = static_cast<uint16_t>(g_ctrl_cfg.getUInt("fan_circ_dur", g_cfg_fan_circ_duration_min));
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
  ctrl_publish_cfg_value("discovery_prefix", g_cfg_ctrl_discovery_prefix);
  ctrl_publish_cfg_value("unique_device_id", g_cfg_unique_device_id);
  ctrl_publish_cfg_value("ota_hostname", g_cfg_ctrl_ota_hostname);
  ctrl_publish_cfg_value("ota_password", g_cfg_ctrl_ota_password);
  ctrl_publish_cfg_value("espnow_channel", String(g_cfg_ctrl_espnow_channel));
  ctrl_publish_cfg_value("espnow_lmk", g_cfg_ctrl_espnow_lmk);
  ctrl_publish_cfg_value("devices", g_cfg_ctrl_devices);
  ctrl_publish_cfg_value("allow_ha", g_cfg_ctrl_allow_ha ? "1" : "0");
  ctrl_publish_cfg_value("mqtt_enabled", g_cfg_ctrl_mqtt_enabled ? "1" : "0");
  ctrl_publish_cfg_value("espnow_enabled", g_cfg_ctrl_espnow_enabled ? "1" : "0");
  ctrl_publish_cfg_value("pirateweather_api_key", g_cfg_ctrl_pirateweather_api_key);
  ctrl_publish_cfg_value("pirateweather_zip", g_cfg_ctrl_pirateweather_zip);
  ctrl_publish_cfg_value("temperature_unit", g_ctrl_temp_unit_f ? "f" : "c");
  ctrl_publish_cfg_value("fan_circulate_period", String(g_cfg_fan_circ_period_min));
  ctrl_publish_cfg_value("fan_circulate_duration", String(g_cfg_fan_circ_duration_min));
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
  } else if (key == "discovery_prefix") {
    g_cfg_ctrl_discovery_prefix = value;
    g_ctrl_cfg.putString("disc_pref", value);
    g_ctrl_mqtt_discovery_sent = false;
  } else if (key == "unique_device_id") {
    g_cfg_unique_device_id = value;
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
  } else if (key == "espnow_lmk") {
    g_cfg_ctrl_espnow_lmk = value;
    g_ctrl_cfg.putString("esp_lmk", value);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "devices") {
    g_cfg_ctrl_devices = value;
    g_ctrl_cfg.putString("devices", value);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "device_add") {
    String entry = value;
    entry.trim();
    int eq_pos = entry.indexOf('=');
    String mac_part = (eq_pos >= 0) ? entry.substring(0, eq_pos) : entry;
    mac_part.trim();
    if (mac_part.length() < 17) return false;
    // Only add if not already present
    if (g_cfg_ctrl_devices.indexOf(mac_part) < 0) {
      String updated = g_cfg_ctrl_devices;
      if (updated.length() > 0) updated += ";";
      updated += entry;
      g_cfg_ctrl_devices = updated;
      g_ctrl_cfg.putString("devices", updated);
    }
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "device_remove") {
    String mac = value;
    mac.trim();
    if (mac.length() < 17) return false;
    String remaining = g_cfg_ctrl_devices;
    String updated = "";
    while (remaining.length() > 0) {
      int semi = remaining.indexOf(';');
      String entry = (semi >= 0) ? remaining.substring(0, semi) : remaining;
      remaining = (semi >= 0) ? remaining.substring(semi + 1) : "";
      entry.trim();
      if (entry.length() == 0) continue;
      int eq_pos = entry.indexOf('=');
      String entry_mac = (eq_pos >= 0) ? entry.substring(0, eq_pos) : entry;
      entry_mac.trim();
      if (entry_mac == mac) continue;
      if (updated.length() > 0) updated += ";";
      updated += entry;
    }
    g_cfg_ctrl_devices = updated;
    g_ctrl_cfg.putString("devices", updated);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "allow_ha") {
    g_cfg_ctrl_allow_ha = (strcmp(raw_value, "1") == 0);
    g_ctrl_cfg.putBool("allow_ha", g_cfg_ctrl_allow_ha);
  } else if (key == "mqtt_enabled") {
    g_cfg_ctrl_mqtt_enabled = (strcmp(raw_value, "1") == 0);
    g_ctrl_cfg.putBool("mqtt_en", g_cfg_ctrl_mqtt_enabled);
    if (!g_cfg_ctrl_mqtt_enabled && g_ctrl_mqtt.connected()) {
      g_ctrl_mqtt.disconnect();
    }
    g_ctrl_mqtt_reconfigure_required = true;
  } else if (key == "espnow_enabled") {
    g_cfg_ctrl_espnow_enabled = (strcmp(raw_value, "1") == 0);
    g_ctrl_cfg.putBool("espnow_en", g_cfg_ctrl_espnow_enabled);
    g_ctrl_cfg_reboot_required = true;
  } else if (key == "pirateweather_api_key") {
    g_cfg_ctrl_pirateweather_api_key = value;
    g_ctrl_cfg.putString("pw_key", value);
    g_ctrl_last_weather_poll_ms = 0;
  } else if (key == "pirateweather_zip") {
    g_cfg_ctrl_pirateweather_zip = value;
    g_ctrl_cfg.putString("pw_zip", value);
    g_ctrl_last_weather_poll_ms = 0;
  } else if (key == "temperature_unit") {
    g_ctrl_temp_unit_f = (value == "f" || value == "fahrenheit");
    g_ctrl_cfg.putBool("temp_u_f", g_ctrl_temp_unit_f);
    g_ctrl_mqtt_discovery_sent = false;
  } else if (key == "fan_circulate_period") {
    const long parsed = atol(raw_value);
    if (parsed < 10 || parsed > 120) return false;
    g_cfg_fan_circ_period_min = static_cast<uint16_t>(parsed);
    g_ctrl_cfg.putUInt("fan_circ_per", g_cfg_fan_circ_period_min);
    if (g_controller != nullptr) {
      g_controller->app().runtime_mut().set_fan_circulate_period_min(g_cfg_fan_circ_period_min);
    }
  } else if (key == "fan_circulate_duration") {
    const long parsed = atol(raw_value);
    if (parsed < 1 || parsed > 30) return false;
    g_cfg_fan_circ_duration_min = static_cast<uint16_t>(parsed);
    g_ctrl_cfg.putUInt("fan_circ_dur", g_cfg_fan_circ_duration_min);
    if (g_controller != nullptr) {
      g_controller->app().runtime_mut().set_fan_circulate_duration_min(g_cfg_fan_circ_duration_min);
    }
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

  g_controller->app().set_outdoor_weather(outdoor_temp_c, icon);
  g_controller->transport().publish_weather(outdoor_temp_c, icon);

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
  const String dev_id = g_cfg_unique_device_id + "_controller";
  const String dp = g_cfg_ctrl_discovery_prefix + "/";
  const String switch_topic = dp + "switch/" + dev_id + "_lockout/config";
  const String filter_topic = dp + "sensor/" + dev_id + "_filter_runtime/config";
  const String state_topic = dp + "sensor/" + dev_id + "_furnace_state/config";
  const String fw_topic = dp + "sensor/" + dev_id + "_controller_firmware/config";
  const String rssi_topic = dp + "sensor/" + dev_id + "_controller_wifi_rssi/config";
  const String heap_topic = dp + "sensor/" + dev_id + "_controller_free_heap/config";
  const String last_mqtt_cmd_topic =
      dp + "sensor/" + dev_id + "_controller_last_mqtt_command/config";
  const String last_espnow_rx_topic =
      dp + "sensor/" + dev_id + "_controller_last_espnow_rx/config";
  const String espnow_ok_topic =
      dp + "sensor/" + dev_id + "_controller_espnow_send_ok/config";
  const String espnow_fail_topic =
      dp + "sensor/" + dev_id + "_controller_espnow_send_fail/config";
  const String err_mqtt_topic =
      dp + "sensor/" + dev_id + "_controller_error_mqtt/config";
  const String err_ota_topic =
      dp + "sensor/" + dev_id + "_controller_error_ota/config";
  const String err_espnow_topic =
      dp + "sensor/" + dev_id + "_controller_error_espnow/config";
  const String reset_seq_topic =
      dp + "button/" + dev_id + "_controller_reset_sequence/config";
  const String reboot_topic =
      dp + "button/" + dev_id + "_controller_reboot/config";
  const String filter_change_topic =
      dp + "binary_sensor/" + dev_id + "_filter_change_required/config";

  const String climate_topic = dp + "climate/" + dev_id + "/config";

  char payload[1500];
  // Use HA's ~ (tilde) abbreviation to keep the climate payload compact.
  // All topic references starting with ~ are expanded by HA using the base topic.
  // Unit-dependent fields: temp_unit, temp_step, min_temp, max_temp.
  const char *unit_str = g_ctrl_temp_unit_f ? "F" : "C";
  const char *step_str = g_ctrl_temp_unit_f ? "1" : "0.5";
  int min_temp = g_ctrl_temp_unit_f ? 41 : 5;
  int max_temp = g_ctrl_temp_unit_f ? 95 : 35;
  snprintf(
      payload, sizeof(payload),
      "{\"~\":\"%s\",\"name\":\"Furnace Thermostat\",\"uniq_id\":\"%s_climate\","
      "\"mode_cmd_t\":\"~/cmd/mode\",\"mode_stat_t\":\"~/state/mode\","
      "\"temp_cmd_t\":\"~/cmd/target_temp_c\",\"temp_stat_t\":\"~/state/target_temp_c\","
      "\"curr_temp_t\":\"~/state/current_temp_c\","
      "\"fan_mode_cmd_t\":\"~/cmd/fan_mode\",\"fan_mode_stat_t\":\"~/state/fan_mode\","
      "\"curr_hum_t\":\"~/state/current_humidity\","
      "\"modes\":[\"off\",\"heat\",\"cool\"],"
      "\"fan_modes\":[\"auto\",\"on\",\"circulate\"],"
      "\"avty_t\":\"~/state/availability\","
      "\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\","
      "\"min_temp\":%d,\"max_temp\":%d,\"temp_step\":%s,\"temp_unit\":\"%s\","
      "\"dev\":{\"ids\":[\"%s\"],"
      "\"name\":\"Furnace Controller\",\"mf\":\"rgregg\",\"mdl\":\"ESP32 Thermostat\"}}",
      base.c_str(), dev_id.c_str(),
      min_temp, max_temp, step_str, unit_str,
      dev_id.c_str());
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
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(rssi_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Free Heap\",\"uniq_id\":\"%s_controller_free_heap\","
           "\"stat_t\":\"%s/state/free_heap_bytes\",\"unit_of_meas\":\"B\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(heap_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Last MQTT Command\",\"uniq_id\":\"%s_controller_last_mqtt_command\","
           "\"stat_t\":\"%s/state/last_mqtt_command_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(last_mqtt_cmd_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Last ESP-NOW RX\",\"uniq_id\":\"%s_controller_last_espnow_rx\","
           "\"stat_t\":\"%s/state/last_espnow_rx_ms\",\"unit_of_meas\":\"ms\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(last_espnow_rx_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller ESP-NOW Send OK\",\"uniq_id\":\"%s_controller_espnow_send_ok\","
           "\"stat_t\":\"%s/state/espnow_send_ok_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(espnow_ok_topic.c_str(), payload, true);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller ESP-NOW Send Fail\",\"uniq_id\":\"%s_controller_espnow_send_fail\","
           "\"stat_t\":\"%s/state/espnow_send_fail_count\",\"icon\":\"mdi:counter\","
           "\"entity_category\":\"diagnostic\",\"en\":false,\"dev\":{\"ids\":[\"%s\"]}}",
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

  // Fan circulation config number entities
  {
    String topic = dp + "number/" + dev_id + "_fan_circulate_period/config";
    snprintf(payload, sizeof(payload),
             "{\"~\":\"%s\",\"name\":\"Fan Circulate Period\","
             "\"uniq_id\":\"%s_fan_circulate_period\","
             "\"cmd_t\":\"~/cfg/fan_circulate_period/set\","
             "\"stat_t\":\"~/cfg/fan_circulate_period/state\","
             "\"min\":10,\"max\":120,\"step\":5,\"mode\":\"box\",\"unit_of_meas\":\"min\","
             "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"]}}",
             base.c_str(), dev_id.c_str(), dev_id.c_str());
    g_ctrl_mqtt.publish(topic.c_str(), payload, true);

    topic = dp + "number/" + dev_id + "_fan_circulate_duration/config";
    snprintf(payload, sizeof(payload),
             "{\"~\":\"%s\",\"name\":\"Fan Circulate Duration\","
             "\"uniq_id\":\"%s_fan_circulate_duration\","
             "\"cmd_t\":\"~/cfg/fan_circulate_duration/set\","
             "\"stat_t\":\"~/cfg/fan_circulate_duration/state\","
             "\"min\":1,\"max\":30,\"step\":1,\"mode\":\"box\",\"unit_of_meas\":\"min\","
             "\"entity_category\":\"config\",\"dev\":{\"ids\":[\"%s\"]}}",
             base.c_str(), dev_id.c_str(), dev_id.c_str());
    g_ctrl_mqtt.publish(topic.c_str(), payload, true);
  }

  // Relay state binary sensors
  static const char *relay_names[] = {"Heat Relay", "Cool Relay", "Fan Relay"};
  static const char *relay_ids[] = {"relay_heat", "relay_cool", "relay_fan"};
  for (int i = 0; i < 3; ++i) {
    String topic = dp + "binary_sensor/" + dev_id + "_" + relay_ids[i] + "/config";
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\","
             "\"stat_t\":\"%s/state/%s\",\"icon\":\"mdi:electric-switch\","
             "\"entity_category\":\"diagnostic\",\"dev\":{\"ids\":[\"%s\"]}}",
             relay_names[i], dev_id.c_str(), relay_ids[i],
             base.c_str(), relay_ids[i], dev_id.c_str());
    g_ctrl_mqtt.publish(topic.c_str(), payload, true);
  }

  g_ctrl_mqtt_discovery_sent = true;
}

// html_escape() and json_escape() are now in web_ui namespace via web_ui_escape.h

// Audit log: MQTT publish callback and helpers
void ctrl_audit_publish(const char *msg) {
  if (g_ctrl_mqtt.connected()) {
    g_ctrl_mqtt.publish(ctrl_topic_for("state/audit").c_str(), msg, false);
  }
}

void ctrl_audit(const char *fmt, ...) {
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  g_audit_log.add(millis(), "%s", buf);
}

void ctrl_runtime_audit_bridge(const char *msg) {
  // Bridge from runtime audit callback → global audit log with timestamp
  g_audit_log.add(millis(), "%s", msg);
}

void ctrl_web_handle_log_get() {
  String body;
  body.reserve(g_audit_log.count() * 80);
  for (size_t i = 0; i < g_audit_log.count(); ++i) {
    body += g_audit_log.entry(i);
    body += '\n';
  }
  g_ctrl_web.send(200, "text/plain", body);
}

void ctrl_web_handle_config_get() {
  String body = "{\"controller\":{";
  body += "\"wifi_ssid\":\"" + web_ui::json_escape(g_cfg_ctrl_wifi_ssid) + "\",";
  body += "\"wifi_password\":\"" + String(g_cfg_ctrl_wifi_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_host\":\"" + web_ui::json_escape(g_cfg_ctrl_mqtt_host) + "\",";
  body += "\"mqtt_port\":" + String(g_cfg_ctrl_mqtt_port) + ",";
  body += "\"mqtt_user\":\"" + web_ui::json_escape(g_cfg_ctrl_mqtt_user) + "\",";
  body += "\"mqtt_password\":\"" + String(g_cfg_ctrl_mqtt_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"mqtt_client_id\":\"" + web_ui::json_escape(g_cfg_ctrl_mqtt_client_id) + "\",";
  body += "\"mqtt_base_topic\":\"" + web_ui::json_escape(g_cfg_ctrl_mqtt_base_topic) + "\",";
  body += "\"display_mqtt_base_topic\":\"" + web_ui::json_escape(g_cfg_display_mqtt_base_topic) + "\",";
  body += "\"discovery_prefix\":\"" + web_ui::json_escape(g_cfg_ctrl_discovery_prefix) + "\",";
  body += "\"unique_device_id\":\"" + web_ui::json_escape(g_cfg_unique_device_id) + "\",";
  body += "\"ota_hostname\":\"" + web_ui::json_escape(g_cfg_ctrl_ota_hostname) + "\",";
  body += "\"ota_password\":\"" + String(g_cfg_ctrl_ota_password.length() > 0 ? "set" : "unset") + "\",";
  body += "\"espnow_channel\":" + String(g_cfg_ctrl_espnow_channel) + ",";
  body += "\"espnow_lmk\":\"" + String(g_cfg_ctrl_espnow_lmk.length() > 0 ? "set" : "unset") + "\",";
  body += "\"devices\":\"" + web_ui::json_escape(g_cfg_ctrl_devices) + "\",";
  body += "\"allow_ha\":" + String(g_cfg_ctrl_allow_ha ? "true" : "false") + ",";
  body += "\"mqtt_enabled\":" + String(g_cfg_ctrl_mqtt_enabled ? "true" : "false") + ",";
  body += "\"espnow_enabled\":" + String(g_cfg_ctrl_espnow_enabled ? "true" : "false") + ",";
  body += "\"pirateweather_api_key\":\"" +
          String(g_cfg_ctrl_pirateweather_api_key.length() > 0 ? "set" : "unset") + "\",";
  body += "\"pirateweather_zip\":\"" + web_ui::json_escape(g_cfg_ctrl_pirateweather_zip) + "\",";
  body += "\"reboot_required\":" + String(g_ctrl_cfg_reboot_required ? "true" : "false");
  body += "}}";
  g_ctrl_web.send(200, "application/json", body);
}

void ctrl_web_handle_config_post() {
  int updated = 0;
  for (int i = 0; i < g_ctrl_web.args(); ++i) {
    const String name = g_ctrl_web.argName(i);
    const String value = g_ctrl_web.arg(i);
    if (ctrl_try_update_runtime_config(name, value.c_str())) {
      ++updated;
    }
  }
  String body = "updated=" + String(updated) + "\n";
  g_ctrl_web.send(200, "text/plain", body);
}

void ctrl_web_handle_reboot_post() {
  ctrl_schedule_reboot();
  g_ctrl_web.send(200, "text/plain", "rebooting\n");
}

void ctrl_web_handle_status_get() {
  if (g_controller == nullptr) {
    g_ctrl_web.send(503, "application/json", "{\"error\":\"controller not initialized\"}");
    return;
  }
  const uint32_t now = millis();
  const auto &app = g_controller->app();
  const auto &rt = app.runtime();
  char temp_str[16], hum_str[16];
  if (app.has_indoor_temperature()) {
    snprintf(temp_str, sizeof(temp_str), "%.2f", static_cast<double>(app.indoor_temperature_c()));
  } else {
    strcpy(temp_str, "null");
  }
  if (app.has_indoor_humidity()) {
    snprintf(hum_str, sizeof(hum_str), "%.2f", static_cast<double>(app.indoor_humidity_pct()));
  } else {
    strcpy(hum_str, "null");
  }
  char buf[1200];
  snprintf(buf, sizeof(buf),
    "{"
    "\"uptime_ms\":%lu,"
    "\"wifi_connected\":%s,"
    "\"wifi_ip\":\"%s\","
    "\"wifi_rssi\":%d,"
    "\"mqtt_connected\":%s,"
    "\"has_indoor_temp\":%s,"
    "\"indoor_temp_c\":%s,"
    "\"has_indoor_humidity\":%s,"
    "\"indoor_humidity_pct\":%s,"
    "\"furnace_state\":%u,"
    "\"furnace_state_text\":\"%s\","
    "\"mode\":\"%s\","
    "\"fan_mode\":\"%s\","
    "\"target_temp_c\":%.1f,"
    "\"hvac_lockout\":%s,"
    "\"failsafe_active\":%s,"
    "\"espnow_connected\":%s,"
    "\"heartbeat_last_seen_ms\":%lu,"
    "\"last_mqtt_command_ms\":%lu,"
    "\"allow_ha\":%s,"
    "\"mqtt_enabled\":%s,"
    "\"espnow_enabled\":%s,"
    "\"display_availability\":\"%s\","
    "\"filter_runtime_hours\":%.2f,"
    "\"relay_heat\":%s,"
    "\"relay_cool\":%s,"
    "\"relay_fan\":%s,"
    "\"free_heap\":%lu,"
    "\"firmware_version\":\"%s\""
    "}",
    static_cast<unsigned long>(now),
    WiFi.isConnected() ? "true" : "false",
    WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "",
    WiFi.isConnected() ? WiFi.RSSI() : 0,
    g_ctrl_mqtt.connected() ? "true" : "false",
    app.has_indoor_temperature() ? "true" : "false",
    temp_str,
    app.has_indoor_humidity() ? "true" : "false",
    hum_str,
    static_cast<unsigned>(rt.furnace_state()),
    mqtt_payload::furnace_state_to_str(rt.furnace_state()),
    mqtt_payload::mode_to_str(rt.mode()),
    mqtt_payload::fan_to_str(rt.fan_mode()),
    static_cast<double>(rt.target_temperature_c()),
    rt.hvac_lockout() ? "true" : "false",
    rt.failsafe_active() ? "true" : "false",
    (rt.heartbeat_last_seen_ms() > 0 && (now - rt.heartbeat_last_seen_ms()) < 30000UL) ? "true" : "false",
    static_cast<unsigned long>(rt.heartbeat_last_seen_ms()),
    static_cast<unsigned long>(g_ctrl_last_mqtt_command_ms),
    g_cfg_ctrl_allow_ha ? "true" : "false",
    g_cfg_ctrl_mqtt_enabled ? "true" : "false",
    g_cfg_ctrl_espnow_enabled ? "true" : "false",
    g_disp_availability.c_str(),
    static_cast<double>(rt.filter_runtime_hours()),
    g_relay_io.latched_output().heat ? "true" : "false",
    g_relay_io.latched_output().cool ? "true" : "false",
    g_relay_io.latched_output().fan ? "true" : "false",
    static_cast<unsigned long>(ESP.getFreeHeap()),
    THERMOSTAT_FIRMWARE_VERSION
  );
  g_ctrl_web.send(200, "application/json", buf);
}

void ctrl_web_handle_devices_get() {
  String json = "[";
  bool first = true;
  for (size_t i = 0; i < kMaxRegistryEntries; ++i) {
    const auto &e = g_device_registry.entries[i];
    if (!e.occupied) continue;
    if (!first) json += ',';
    first = false;
    json += "{\"mac\":\"";
    json += web_ui::json_escape(e.mac);
    json += "\",\"name\":\"";
    json += web_ui::json_escape(e.name);
    json += "\",\"type\":\"";
    json += web_ui::json_escape(e.type);
    json += "\",\"ip\":\"";
    json += web_ui::json_escape(e.ip);
    json += "\"}";
  }
  json += "]";
  g_ctrl_web.send(200, "application/json", json);
}

void ctrl_web_handle_root() {
  using namespace web_ui;
  String html;
  html.reserve(16384);

  static const TabDef tabs[] = {
    {"status", "Status"},
    {"wifi", "WiFi"},
    {"mqtt", "MQTT"},
    {"weather", "Weather"},
    {"hw", "Hardware"},
    {"system", "System"},
  };
  page_begin(html, "Furnace Controller", g_cfg_ctrl_ota_hostname.c_str(),
             tabs, sizeof(tabs) / sizeof(tabs[0]));

  // ── Status tab ──
  tab_begin(html, "status", true);
  card_begin(html, "Controller Status");
  status_grid_begin(html);
  status_section(html, "Connectivity");
  status_item(html, "WiFi", "wifi_connected");
  status_item(html, "IP Address", "wifi_ip");
  status_item(html, "RSSI", "wifi_rssi");
  status_item(html, "MQTT", "mqtt_connected");
  status_item(html, "ESP-NOW", "espnow_connected");
  status_item(html, "Display", "display_availability");
  status_section(html, "Temperature");
  status_item(html, "Indoor Temp", "indoor_temp_c");
  status_item(html, "Humidity", "indoor_humidity_pct");
  status_item(html, "Target Temp", "target_temp_c");
  status_item(html, "Has Temp", "has_indoor_temp");
  status_section(html, "Furnace");
  status_item(html, "State", "furnace_state");
  status_item(html, "Mode", "mode");
  status_item(html, "Fan", "fan_mode");
  status_item(html, "HVAC Lockout", "hvac_lockout");
  status_item(html, "Failsafe", "failsafe_active");
  status_item(html, "Filter Hours", "filter_runtime_hours");
  status_item(html, "Heat Relay", "relay_heat");
  status_item(html, "Cool Relay", "relay_cool");
  status_item(html, "Fan Relay", "relay_fan");
  status_section(html, "System");
  status_item(html, "Free Heap", "free_heap");
  status_item(html, "Uptime", "uptime_ms");
  status_item(html, "Heartbeat", "heartbeat_last_seen_ms");
  status_item(html, "Firmware", "firmware_version");
  status_section(html, "Debug");
  status_item(html, "Allow HA", "allow_ha");
  status_item(html, "MQTT Enabled", "mqtt_enabled");
  status_item(html, "ESP-NOW Enabled", "espnow_enabled");
  status_grid_end(html);
  card_end(html);
  if (g_ctrl_cfg_reboot_required) {
    html += F("<div class=\"card\" style=\"border:1px solid var(--wn)\">"
              "<p style=\"color:var(--wn)\">Reboot required for pending changes.</p></div>");
  }
  tab_end(html);

  // ── WiFi tab ──
  tab_begin(html, "wifi");
  card_begin(html, "WiFi Settings");
  form_begin(html);
  text_field(html, "WiFi SSID", "wifi_ssid", g_cfg_ctrl_wifi_ssid, nullptr, nullptr, nullptr, 64);
  password_field(html, "WiFi Password", "wifi_password", g_cfg_ctrl_wifi_password.length() > 0);
  form_end(html, "Save WiFi");
  card_end(html);
  tab_end(html);

  // ── MQTT tab ──
  tab_begin(html, "mqtt");
  card_begin(html, "MQTT Broker");
  form_begin(html);
  text_field(html, "Broker Host", "mqtt_host", g_cfg_ctrl_mqtt_host);
  number_field(html, "Broker Port", "mqtt_port", String(g_cfg_ctrl_mqtt_port), "1", "65535", "1");
  text_field(html, "Username", "mqtt_user", g_cfg_ctrl_mqtt_user);
  password_field(html, "Password", "mqtt_password", g_cfg_ctrl_mqtt_password.length() > 0);
  text_field(html, "Client ID", "mqtt_client_id", g_cfg_ctrl_mqtt_client_id);
  text_field(html, "Base Topic", "mqtt_base_topic", g_cfg_ctrl_mqtt_base_topic,
             "e.g. thermostat/furnace-controller");
  text_field(html, "Display Base Topic", "display_mqtt_base_topic",
             g_cfg_display_mqtt_base_topic,
             "MQTT base topic of the thermostat display");
  text_field(html, "Discovery Prefix", "discovery_prefix", g_cfg_ctrl_discovery_prefix,
             "HA MQTT discovery prefix, e.g. homeassistant");
  form_end(html, "Save MQTT");
  card_end(html);
  tab_end(html);

  // ── Weather tab ──
  tab_begin(html, "weather");
  card_begin(html, "PirateWeather");
  form_begin(html);
  password_field(html, "API Key", "pirateweather_api_key",
                 g_cfg_ctrl_pirateweather_api_key.length() > 0);
  text_field(html, "ZIP Code", "pirateweather_zip", g_cfg_ctrl_pirateweather_zip,
             "US ZIP: 12345 or 12345-6789",
             "^[0-9]{5}(-[0-9]{4})?$", "US ZIP format");
  form_end(html, "Save Weather");
  card_end(html);
  tab_end(html);

  // ── Hardware tab ──
  tab_begin(html, "hw");

  // Devices card
  card_begin(html, "Devices");
  // Devices table
  html += F("<table style=\"width:100%;font-size:0.8rem;border-collapse:collapse;margin-bottom:0.75rem\">");
  html += F("<tr>"
            "<th style=\"text-align:left;padding:0.3rem;border-bottom:1px solid var(--bd)\">MAC</th>"
            "<th style=\"text-align:left;padding:0.3rem;border-bottom:1px solid var(--bd)\">Role</th>"
            "<th style=\"text-align:right;padding:0.3rem;border-bottom:1px solid var(--bd)\">Action</th>"
            "</tr>");
  {
    String rem = g_cfg_ctrl_devices;
    while (rem.length() > 0) {
      int semi = rem.indexOf(';');
      String entry = (semi >= 0) ? rem.substring(0, semi) : rem;
      rem = (semi >= 0) ? rem.substring(semi + 1) : "";
      entry.trim();
      if (entry.length() == 0) continue;
      int eq_pos = entry.indexOf('=');
      String mac = (eq_pos >= 0) ? entry.substring(0, eq_pos) : entry;
      String role = (eq_pos >= 0) ? entry.substring(eq_pos + 1) : "";
      mac.trim(); role.trim();
      html += F("<tr><td style=\"padding:0.3rem;font-family:monospace\">");
      html += web_ui::html_escape(mac);
      html += F("</td><td style=\"padding:0.3rem\">");
      html += (role == "temp") ? String("display & temp") : String("display");
      html += F("</td><td style=\"text-align:right;padding:0.3rem\">");
      html += F("<button type=\"button\" class=\"btn btn-d\""
                " style=\"padding:0.2rem 0.5rem;font-size:0.7rem\""
                " onclick=\"removeDevice('");
      html += web_ui::html_escape(mac);
      html += F("')\">Remove</button>");
      html += F("</td></tr>");
    }
  }
  html += F("</table>");
  // Add Device sub-form
  html += F("<div style=\"border-top:1px solid var(--bd);padding-top:0.75rem;"
            "margin-bottom:0.5rem;font-size:0.8rem;font-weight:600\">Add Device</div>");
  html += F("<form onsubmit=\"return submitDeviceAdd(this)\">");
  mac_field(html, "MAC Address", "add_mac", "");
  {
    static const SelectOption role_opts[] = {{"", "Display"}, {"temp", "Display & Temp"}};
    select_field(html, "Role", "add_role", role_opts, 2, "");
  }
  form_end(html, "Add");
  card_end(html);

  // Transport / HA card
  card_begin(html, "Transport");
  form_begin(html);
  checkbox_field(html, "Allow Home Assistant commands", "allow_ha", g_cfg_ctrl_allow_ha);
  checkbox_field(html, "Enable MQTT", "mqtt_enabled", g_cfg_ctrl_mqtt_enabled);
  checkbox_field(html, "Enable ESP-NOW", "espnow_enabled", g_cfg_ctrl_espnow_enabled);
  form_end(html, "Save");
  card_end(html);

  // ESP-NOW Settings card
  card_begin(html, "ESP-NOW Settings");
  form_begin(html);
  number_field(html, "Channel", "espnow_channel", String(g_cfg_ctrl_espnow_channel), "1", "14", "1");
  password_field(html, "Encryption Key (LMK)", "espnow_lmk",
                 g_cfg_ctrl_espnow_lmk.length() > 0,
                 "^[0-9A-Fa-f]{32}$", "32 hex characters");
  form_end(html, "Save ESP-NOW");
  card_end(html);

  // Sensor & OTA card
  card_begin(html, "Sensor & OTA");
  form_begin(html);
  text_field(html, "OTA Hostname", "ota_hostname", g_cfg_ctrl_ota_hostname);
  password_field(html, "OTA Password", "ota_password", g_cfg_ctrl_ota_password.length() > 0);
  form_end(html, "Save");
  card_end(html);

  tab_end(html);

  // ── System tab ──
  tab_begin(html, "system");
  card_begin(html, "Firmware Update");
  html += F("<form method=\"post\" action=\"/update\" enctype=\"multipart/form-data\">");
  file_upload(html);
  html += F("<div class=\"mt\"><button type=\"submit\" class=\"btn btn-p\">Upload Firmware</button></div>");
  html += F("</form>");
  card_end(html);

  card_begin(html, "Device Control");
  reboot_button(html, "Reboot Controller");
  card_end(html);

  card_begin(html, "Links");
  html += F("<p style=\"font-size:0.85rem\"><a href=\"/config\" style=\"color:var(--ac)\">JSON Config</a>"
            " &middot; <a href=\"/status\" style=\"color:var(--ac)\">JSON Status</a>"
            " &middot; <a href=\"/log\" style=\"color:var(--ac)\">Audit Log</a></p>");
  card_end(html);
  tab_end(html);

  page_end(html);
  g_ctrl_web.send(200, "text/html", html);
}

void ctrl_ensure_web_ready() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!g_ctrl_web_started) {
    g_ctrl_web.on("/", HTTP_GET, ctrl_web_handle_root);
    g_ctrl_web.on("/status", HTTP_GET, ctrl_web_handle_status_get);
    g_ctrl_web.on("/devices", HTTP_GET, ctrl_web_handle_devices_get);
    g_ctrl_web.on("/log", HTTP_GET, ctrl_web_handle_log_get);
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
  {
    float target = rt.target_temperature_c();
    if (g_ctrl_temp_unit_f) target = target * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), g_ctrl_temp_unit_f ? "%.0f" : "%.1f", static_cast<double>(target));
    g_ctrl_mqtt.publish(ctrl_topic_for("state/target_temp_c").c_str(), buf, true);
  }
  const auto &app = g_controller->app();
  if (app.has_indoor_temperature()) {
    float current = app.indoor_temperature_c();
    if (g_ctrl_temp_unit_f) current = current * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), g_ctrl_temp_unit_f ? "%.0f" : "%.1f", static_cast<double>(current));
    g_ctrl_mqtt.publish(ctrl_topic_for("state/current_temp_c").c_str(), buf, true);
  }
  if (app.has_indoor_humidity()) {
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(app.indoor_humidity_pct()));
    g_ctrl_mqtt.publish(ctrl_topic_for("state/current_humidity").c_str(), buf, true);
  }
  snprintf(buf, sizeof(buf), "%.2f", rt.filter_runtime_hours());
  g_ctrl_mqtt.publish(ctrl_topic_for("state/filter_runtime_hours").c_str(), buf, true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/filter_change_required").c_str(),
                      rt.filter_runtime_hours() >= kFilterChangeThresholdHours ? "1" : "0", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/max_runtime_exceeded").c_str(),
                      rt.max_runtime_exceeded() ? "1" : "0", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/furnace_state").c_str(),
                      mqtt_payload::furnace_state_to_str(rt.furnace_state()), true);
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
  g_ctrl_mqtt.publish(ctrl_topic_for("state/allow_ha").c_str(),
                      g_cfg_ctrl_allow_ha ? "true" : "false", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/mqtt_enabled").c_str(),
                      g_cfg_ctrl_mqtt_enabled ? "true" : "false", true);
  g_ctrl_mqtt.publish(ctrl_topic_for("state/espnow_enabled").c_str(),
                      g_cfg_ctrl_espnow_enabled ? "true" : "false", true);
  {
    const auto &relay = g_relay_io.latched_output();
    g_ctrl_mqtt.publish(ctrl_topic_for("state/relay_heat").c_str(), relay.heat ? "ON" : "OFF", true);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/relay_cool").c_str(), relay.cool ? "ON" : "OFF", true);
    g_ctrl_mqtt.publish(ctrl_topic_for("state/relay_fan").c_str(), relay.fan ? "ON" : "OFF", true);
  }
  g_ctrl_have_lockout = true;
  g_ctrl_last_lockout = lockout;
}

void ctrl_persist_hvac_state();

bool ctrl_apply_packed_command(uint32_t packed_word, bool from_mqtt,
                               const uint8_t *source_mac = nullptr) {
  if (g_controller == nullptr) {
    return false;
  }

  const thermostat::CommandApplyResult result =
      g_controller->app().on_command_word(packed_word, source_mac);
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
  ctrl_persist_hvac_state();
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

void ctrl_persist_hvac_state() {
  if (!g_ctrl_cfg_ready || !g_ctrl_have_shadow) return;
  // Skip NVS write if nothing has changed
  if (g_ctrl_shadow_mode == g_ctrl_persisted_mode &&
      g_ctrl_shadow_fan == g_ctrl_persisted_fan &&
      g_ctrl_shadow_setpoint_c == g_ctrl_persisted_setpoint_c) return;
  g_ctrl_cfg.putUChar("hvac_mode", static_cast<uint8_t>(g_ctrl_shadow_mode));
  g_ctrl_cfg.putUChar("hvac_fan", static_cast<uint8_t>(g_ctrl_shadow_fan));
  g_ctrl_cfg.putFloat("hvac_sp_c", g_ctrl_shadow_setpoint_c);
  g_ctrl_persisted_mode = g_ctrl_shadow_mode;
  g_ctrl_persisted_fan = g_ctrl_shadow_fan;
  g_ctrl_persisted_setpoint_c = g_ctrl_shadow_setpoint_c;
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
  // Device registry: uses compile-time discovery prefix so all devices share it
  {
    static const String dev_prefix(THERMOSTAT_DEVICE_DISCOVERY_PREFIX "/");
    if (topic_str.startsWith(dev_prefix)) {
      String peer_mac = topic_str.substring(dev_prefix.length());
      // Skip our own MAC
      if (peer_mac != WiFi.macAddress()) {
        // Parse the JSON payload to extract device fields
        char buf[256];
        size_t buf_len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
        memcpy(buf, payload, buf_len);
        buf[buf_len] = '\0';
        char name[48] = "", type[16] = "", ip[16] = "";
        bool ok = json_extract_string(buf, "name", name, sizeof(name))
                && json_extract_string(buf, "type", type, sizeof(type))
                && json_extract_string(buf, "ip", ip, sizeof(ip));
        if (ok) {
          g_device_registry.upsert(peer_mac.c_str(), name, type, ip);
        }
      }
      return;
    }
  }

  if (topic_str == display_topic_for("state/availability")) {
    if (g_disp_availability != value) {
      ctrl_audit("display: %s->%s [mqtt]", g_disp_availability.c_str(), value);
    }
    g_disp_availability = value;
    // Display being online serves as heartbeat — it can relay commands
    if (g_controller != nullptr && strcmp(value, "online") == 0) {
      g_controller->app().on_heartbeat(millis());
    }
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
        if (isfinite(fval) && fval >= -40.0f && fval <= 85.0f) {
          ctrl_audit("indoor_temp: %.1fC [mqtt/sensor %s]",
                     static_cast<double>(fval), sensor_mac);
          g_controller->app().on_indoor_temperature_c(fval, parsed_mac);
          // Valid temperature from primary sensor keeps failsafe from triggering
          g_controller->app().on_heartbeat(millis());
        }
      } else if (strcmp(sensor_suffix, "humidity") == 0) {
        if (isfinite(fval) && fval >= 0.0f && fval <= 100.0f) {
          g_controller->app().on_indoor_humidity(fval, parsed_mac);
        }
      }
    }
    return;
  }

  const bool ha_allowed = g_cfg_ctrl_allow_ha && g_cfg_ctrl_mqtt_enabled;
  if (topic_str.startsWith(ctrl_topic_for("cmd/")) && ha_allowed) {
    g_ctrl_last_mqtt_command_ms = millis();
  }

  if (g_controller == nullptr) {
    return;
  }

  // Block lockout and HVAC commands when HA is not allowed
  if (!ha_allowed) {
    if (topic_str == ctrl_topic_for("cmd/lockout") ||
        topic_str == ctrl_topic_for("cmd/mode") ||
        topic_str == ctrl_topic_for("cmd/fan_mode") ||
        topic_str == ctrl_topic_for("cmd/target_temp_c") ||
        topic_str == ctrl_topic_for("cmd/packed_word") ||
        topic_str == ctrl_topic_for("cmd/sync") ||
        topic_str == ctrl_topic_for("cmd/filter_reset")) {
      return;
    }
  }

  if (topic_str == ctrl_topic_for("cmd/lockout")) {
    const bool new_lockout = mqtt_payload::parse_bool(value);
    ctrl_audit("lockout: %s [mqtt]", new_lockout ? "on" : "off");
    g_controller->app().set_hvac_lockout(new_lockout);
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
    float sp = static_cast<float>(atof(value));
    if (!isfinite(sp)) return;  // reject NaN/Inf
    if (g_ctrl_temp_unit_f) sp = (sp - 32.0f) * 5.0f / 9.0f;
    if (sp < 0.0f) sp = 0.0f;
    if (sp > 40.0f) sp = 40.0f;
    g_ctrl_shadow_setpoint_c = sp;
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
    g_ctrl_mqtt.publish(display_topic_for("cmd/reset_sequence").c_str(), "1", false);
    ctrl_publish_runtime_state();
    return;
  }
  if (topic_str == ctrl_topic_for("cmd/filter_reset") && mqtt_payload::parse_bool(value)) {
    ctrl_apply_mqtt_shadow(false, true);
    return;
  }
  // Thermostat mirrored packed command path (MQTT-primary path).
  // Topic: {display_base}/state/packed_command/{mac}
  {
    const String prefix = display_topic_for("state/packed_command/");
    if (topic_str.startsWith(prefix)) {
      // Display sending commands proves it's alive — update failsafe heartbeat
      if (g_controller != nullptr) {
        g_controller->app().on_heartbeat(millis());
      }
      if (!g_cfg_ctrl_allow_ha || !g_cfg_ctrl_mqtt_enabled) return;
      uint32_t packed = 0;
      if (ctrl_parse_u32_payload(value, &packed)) {
        uint8_t src_mac[6];
        const char *mac_str = topic + prefix.length();
        const uint8_t *mac_ptr = ctrl_parse_mac(mac_str, src_mac) ? src_mac : nullptr;
        ctrl_apply_packed_command(packed, true, mac_ptr);
      }
      return;
    }
  }
}

void ctrl_start_wifi_provisioning() {
  if (g_ctrl_wifi_provisioning_started) return;
#ifdef IMPROV_WIFI_BLE_ENABLED
  WiFi.disconnect(true);
  Serial.printf("[Improv] Free internal RAM: %u, largest block: %u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  ImprovBleConfig cfg = {};
  cfg.device_name = "Controller";
  cfg.firmware_name = THERMOSTAT_PROJECT_NAME;
  cfg.firmware_version = THERMOSTAT_FIRMWARE_VERSION;
  cfg.hardware_variant = "ESP32";
  cfg.device_url = nullptr;
  improv_ble_start(cfg, [](const char *ssid, const char *password) {
    g_cfg_ctrl_wifi_ssid = ssid;
    g_cfg_ctrl_wifi_password = password;
    g_ctrl_cfg.putString("wifi_ssid", ssid);
    g_ctrl_cfg.putString("wifi_pwd", password);
    g_ctrl_wifi_reconnect_required = true;
  });
#endif
  g_ctrl_wifi_provisioning_started = true;
}

void ctrl_wifi_event_handler(arduino_event_t *event) {
  if (event == nullptr) return;
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#ifdef IMPROV_WIFI_BLE_ENABLED
      if (improv_ble_is_active()) {
        improv_ble_stop();
      }
#endif
      g_ctrl_wifi_provisioning_started = false;
      break;
    default:
      break;
  }
}

void ctrl_ensure_wifi_connected(uint32_t now_ms) {
  if (g_ctrl_wifi_reconnect_required) {
    g_ctrl_wifi_reconnect_required = false;
    WiFi.disconnect();
    g_ctrl_last_wifi_attempt_ms = 0;
  }
  if (WiFi.status() == WL_CONNECTED) return;

  // Phase 1: Use configured SSID if available
  if (g_cfg_ctrl_wifi_ssid.length() > 0) {
    if ((now_ms - g_ctrl_last_wifi_attempt_ms) < kCtrlNetworkRetryMs) return;
    g_ctrl_last_wifi_attempt_ms = now_ms;
    WiFi.begin(g_cfg_ctrl_wifi_ssid.c_str(), g_cfg_ctrl_wifi_password.c_str());
    return;
  }

  // Phase 2: If provisioning is running, let it handle things
  if (g_ctrl_wifi_provisioning_started) return;

  // Phase 3: Try stored creds via WiFi.begin()
  if (!g_ctrl_wifi_has_attempted_stored_connect) {
    g_ctrl_wifi_has_attempted_stored_connect = true;
    g_ctrl_first_wifi_attempt_ms = now_ms;
    g_ctrl_last_wifi_attempt_ms = now_ms;
    WiFi.begin();
    return;
  }

  // Phase 4: After timeout, start BLE provisioning
  if ((now_ms - g_ctrl_first_wifi_attempt_ms) >= kCtrlProvisionStartDelayMs) {
    ctrl_start_wifi_provisioning();
    return;
  }

  if ((now_ms - g_ctrl_last_wifi_attempt_ms) < kCtrlNetworkRetryMs) return;
  g_ctrl_last_wifi_attempt_ms = now_ms;
  WiFi.begin();
}

void ctrl_ensure_mqtt_connected(uint32_t now_ms) {
  if (g_cfg_ctrl_mqtt_host.length() == 0 || !g_cfg_ctrl_mqtt_enabled) {
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
  String will_topic = ctrl_topic_for("state/availability");
  if (g_cfg_ctrl_mqtt_user.length() == 0) {
    ok = g_ctrl_mqtt.connect(g_cfg_ctrl_mqtt_client_id.c_str(),
                             will_topic.c_str(), 1, true, "offline");
  } else {
    ok = g_ctrl_mqtt.connect(g_cfg_ctrl_mqtt_client_id.c_str(),
                             g_cfg_ctrl_mqtt_user.c_str(), g_cfg_ctrl_mqtt_password.c_str(),
                             will_topic.c_str(), 1, true, "offline");
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
  g_ctrl_mqtt.subscribe(ctrl_topic_for("sensor/+/temp_c").c_str());
  g_ctrl_mqtt.subscribe(ctrl_topic_for("sensor/+/humidity").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/packed_command/+").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/availability").c_str());
  g_ctrl_mqtt.subscribe(THERMOSTAT_DEVICE_DISCOVERY_PREFIX "/+");
  ctrl_publish_discovery();
  ctrl_publish_all_cfg_state();
  ctrl_publish_runtime_state();

  // Device registry: publish our identity so other devices/tools can discover us
  {
    String mac = WiFi.macAddress();
    String reg_topic = String(THERMOSTAT_DEVICE_DISCOVERY_PREFIX "/") + mac;
    char reg_buf[256];
    snprintf(reg_buf, sizeof(reg_buf),
             "{\"mac\":\"%s\",\"ip\":\"%s\",\"type\":\"controller\","
             "\"name\":\"%s\",\"base_topic\":\"%s\",\"firmware\":\"%s\"}",
             mac.c_str(),
             WiFi.localIP().toString().c_str(),
             g_cfg_ctrl_ota_hostname.c_str(),
             g_cfg_ctrl_mqtt_base_topic.c_str(),
             THERMOSTAT_FIRMWARE_VERSION);
    g_ctrl_mqtt.publish(reg_topic.c_str(), reg_buf, true);
  }
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
      ctrl_audit("ota_arduino: failed err=%u", static_cast<unsigned>(error));
    });
    ArduinoOTA.onEnd([]() {
      g_ctrl_last_ota_error = "none";
      ctrl_audit("ota_arduino: ok");
    });
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
  delay(500);
  Serial.println("\n\n============================");
  Serial.println("ESP32 Controller Booting...");
  Serial.println("============================");
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
  controller_cfg.fan_circulate_period_min = g_cfg_fan_circ_period_min;
  controller_cfg.fan_circulate_duration_min = g_cfg_fan_circ_duration_min;

  thermostat::EspNowControllerConfig transport_cfg;
  transport_cfg.channel = g_cfg_ctrl_espnow_channel;
  transport_cfg.heartbeat_interval_ms = 10000;
  transport_cfg.peer_count = 0;

  if (g_cfg_ctrl_espnow_enabled) {
    // Parse peer MACs from unified devices list
    transport_cfg.peer_count = ctrl_collect_peer_macs(
        g_cfg_ctrl_devices,
        transport_cfg.peer_macs, static_cast<int>(thermostat::kMaxEspNowPeers),
        ctrl_parse_mac);

    uint8_t lmk[16] = {0};
    if (ctrl_parse_lmk_hex(g_cfg_ctrl_espnow_lmk.c_str(), lmk) &&
        transport_cfg.peer_count > 0) {
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
  }

  static thermostat::ControllerNode node(controller_cfg, transport_cfg);
  g_controller = &node;
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(ctrl_wifi_event_handler);
  WiFi.setAutoReconnect(true);
  g_ctrl_mqtt.setBufferSize(1024);
  g_ctrl_mqtt.setServer(g_cfg_ctrl_mqtt_host.c_str(), g_cfg_ctrl_mqtt_port);
  g_ctrl_mqtt.setCallback(ctrl_mqtt_on_message);

  bool ok = false;
  if (g_cfg_ctrl_espnow_enabled) {
    ok = g_controller->begin();
  }
  g_ctrl_last_espnow_error = ok ? "none" : (g_cfg_ctrl_espnow_enabled ? "begin_failed" : "disabled");

  // Apply temp sensor MAC from devices list (nullptr = auto-claim)
  {
    String temp_mac_str = ctrl_find_temp_sensor_mac_str(g_cfg_ctrl_devices);
    uint8_t ps_mac[6];
    if (temp_mac_str.length() >= 17 && ctrl_parse_mac(temp_mac_str.c_str(), ps_mac)) {
      g_controller->app().set_primary_sensor_mac(ps_mac);
    }
  }

  // Restore persisted HVAC state (mode/fan/setpoint) so the furnace
  // resumes after a power outage without waiting for user interaction.
  if (g_ctrl_cfg_ready && g_ctrl_cfg.isKey("hvac_mode")) {
    g_ctrl_shadow_mode = static_cast<FurnaceMode>(g_ctrl_cfg.getUChar("hvac_mode", 0));
    g_ctrl_shadow_fan = static_cast<FanMode>(g_ctrl_cfg.getUChar("hvac_fan", 0));
    g_ctrl_shadow_setpoint_c = g_ctrl_cfg.getFloat("hvac_sp_c", 20.0f);
    g_ctrl_have_shadow = true;
    // Start MQTT seq high so NVS-restored state wins over any stale retained
    // packed_command from the display (which shares the default seq tracker).
    g_ctrl_mqtt_seq = 0x100;
    ctrl_apply_mqtt_shadow(false, false);
  }

  // Wire audit log
  g_audit_log.set_publish_callback(ctrl_audit_publish);
  g_controller->app().runtime_mut().set_audit_callback(ctrl_runtime_audit_bridge);
  ota_set_audit_callback(ctrl_runtime_audit_bridge);

  g_relay_io.begin();
  Serial.printf("controller_node_begin=%u\n", static_cast<unsigned>(ok));
  ctrl_audit("boot ok, espnow=%s", ok ? "true" : "false");
  ota_rollback_begin();
}

void loop() {
  static uint32_t last_heartbeat = 0;
  const uint32_t now = millis();
  if (now - last_heartbeat >= 5000) {
    last_heartbeat = now;
    Serial.printf("[%lu] controller alive, wifi=%s, mqtt=%s, prov=%s\n",
                  now,
                  WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "no",
                  g_ctrl_mqtt.connected() ? "yes" : "no",
                  g_ctrl_wifi_provisioning_started ? "active" : "idle");
  }
  if (g_ctrl_reboot_requested && static_cast<int32_t>(now - g_ctrl_reboot_at_ms) >= 0) {
    ESP.restart();
  }
  if (g_controller != nullptr) {
    g_controller->tick(now);
    const ThermostatSnapshot snap = g_controller->app().runtime().snapshot();
    g_relay_io.apply(now, snap.relay, snap.failsafe_active || snap.hvac_lockout);
    ctrl_ensure_wifi_connected(now);
    wifi_watchdog_tick(now, g_ctrl_mqtt.connected());
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
