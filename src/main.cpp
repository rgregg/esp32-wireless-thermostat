#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "controller_app.h"
#include "controller_relay_io.h"
#include "controller_node.h"
#include "esp32s3_thermostat_firmware.h"
#include "espnow_cmd_word.h"
#include "thermostat_device_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
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
#define THERMOSTAT_CONTROLLER_MQTT_HOST ""
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
#define THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID "esp32-wireless-thermostat-controller"
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

String ctrl_topic_for(const char *suffix) {
  String out(THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC);
  out += "/";
  out += suffix;
  return out;
}

String display_topic_for(const char *suffix) {
  String out(THERMOSTAT_THERMOSTAT_MQTT_BASE_TOPIC);
  out += "/";
  out += suffix;
  return out;
}

bool ctrl_parse_bool_payload(const char *value) {
  if (value == nullptr) {
    return false;
  }
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0;
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

  const String base = THERMOSTAT_CONTROLLER_MQTT_BASE_TOPIC;
  const String dev_id = THERMOSTAT_MQTT_SHARED_DEVICE_ID;
  const String switch_topic = String("homeassistant/switch/") + dev_id + "_lockout/config";
  const String filter_topic = String("homeassistant/sensor/") + dev_id + "_filter_runtime/config";
  const String state_topic = String("homeassistant/sensor/") + dev_id + "_furnace_state/config";

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

  g_ctrl_mqtt_discovery_sent = true;
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
  if (g_controller == nullptr || topic == nullptr || payload == nullptr) {
    return;
  }

  char value[32];
  const size_t copy_len = (length < sizeof(value) - 1) ? length : sizeof(value) - 1;
  memcpy(value, payload, copy_len);
  value[copy_len] = '\0';

  const String topic_str(topic);
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
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (strlen(THERMOSTAT_CONTROLLER_WIFI_SSID) == 0) {
    return;
  }
  if ((now_ms - g_ctrl_last_wifi_attempt_ms) < kCtrlNetworkRetryMs) {
    return;
  }
  g_ctrl_last_wifi_attempt_ms = now_ms;
  WiFi.begin(THERMOSTAT_CONTROLLER_WIFI_SSID, THERMOSTAT_CONTROLLER_WIFI_PASSWORD);
}

void ctrl_ensure_mqtt_connected(uint32_t now_ms) {
  if (strlen(THERMOSTAT_CONTROLLER_MQTT_HOST) == 0) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED || g_ctrl_mqtt.connected()) {
    return;
  }
  if ((now_ms - g_ctrl_last_mqtt_attempt_ms) < kCtrlNetworkRetryMs) {
    return;
  }
  g_ctrl_last_mqtt_attempt_ms = now_ms;

  bool ok = false;
  if (strlen(THERMOSTAT_CONTROLLER_MQTT_USER) == 0) {
    ok = g_ctrl_mqtt.connect(THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID);
  } else {
    ok = g_ctrl_mqtt.connect(THERMOSTAT_CONTROLLER_MQTT_CLIENT_ID,
                             THERMOSTAT_CONTROLLER_MQTT_USER,
                             THERMOSTAT_CONTROLLER_MQTT_PASSWORD);
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
  g_ctrl_mqtt.subscribe(display_topic_for("state/mode").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/fan_mode").c_str());
  g_ctrl_mqtt.subscribe(display_topic_for("state/target_temp_c").c_str());
  ctrl_publish_discovery();
  ctrl_publish_runtime_state();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  thermostat::ControllerConfig controller_cfg;
  controller_cfg.failsafe_timeout_ms = 300000;
  controller_cfg.fan_circulate_period_min = 60;
  controller_cfg.fan_circulate_duration_min = 10;

  thermostat::EspNowControllerConfig transport_cfg;
  transport_cfg.channel = THERMOSTAT_CONTROLLER_ESPNOW_CHANNEL;
  transport_cfg.heartbeat_interval_ms = 10000;
  if (ctrl_parse_mac(THERMOSTAT_CONTROLLER_ESPNOW_PEER_MAC, transport_cfg.peer_mac)) {
    uint8_t lmk[16] = {0};
    if (ctrl_parse_lmk_hex(THERMOSTAT_CONTROLLER_ESPNOW_LMK, lmk)) {
      memcpy(transport_cfg.lmk, lmk, sizeof(lmk));
      transport_cfg.encrypted = true;
    }
  }

  static thermostat::ControllerNode node(controller_cfg, transport_cfg);
  g_controller = &node;
  WiFi.mode(WIFI_STA);
  g_ctrl_mqtt.setServer(THERMOSTAT_CONTROLLER_MQTT_HOST, THERMOSTAT_CONTROLLER_MQTT_PORT);
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
