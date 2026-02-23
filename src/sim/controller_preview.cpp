#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "command_builder.h"
#include "controller/controller_app.h"
#include "controller/controller_relay_io.h"
#include "controller/controller_runtime.h"
#include "espnow_cmd_word.h"
#include "sim_mqtt_client.h"
#include "sim_weather_client.h"
#include "thermostat/thermostat_state.h"

namespace {

constexpr int kDisplayWidth = 700;
constexpr int kDisplayHeight = 500;

// MQTT topics matching real hardware
constexpr const char *kControllerBaseTopic = "thermostat/furnace-controller";
constexpr const char *kDisplayBaseTopic = "thermostat/furnace-display";
constexpr const char *kMqttClientId = "sim-controller";
constexpr const char *kMqttHost = "localhost";
constexpr int kMqttPort = 1883;

// Colors matching PCB appearance
constexpr uint32_t kColorPcbBlue = 0xFF1a2744;
constexpr uint32_t kColorRelayOff = 0xFF444444;
constexpr uint32_t kColorRelayOn = 0xFF00DD00;
constexpr uint32_t kColorRelayBlocked = 0xFF884400;  // Orange for interlock
constexpr uint32_t kColorWhite = 0xFFFFFFFF;
constexpr uint32_t kColorLightBlue = 0xFF4da6ff;
constexpr uint32_t kColorYellow = 0xFFFFCC00;
constexpr uint32_t kColorRed = 0xFFFF4444;
constexpr uint32_t kColorGreen = 0xFF44FF44;
constexpr uint32_t kColorEsp32Gray = 0xFF3a3a3a;
constexpr uint32_t kColorPowerYellow = 0xFFccaa00;
constexpr uint32_t kColorStatusBar = 0xFF0d1a2a;
constexpr uint32_t kColorHeaderBg = 0xFF1e3a5f;
constexpr uint32_t kColorPanelBg = 0xFF152238;

SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
TTF_Font *g_font_large = nullptr;
TTF_Font *g_font_medium = nullptr;
TTF_Font *g_font_small = nullptr;
bool g_running = true;

sim::SimMqttClient g_mqtt;
bool g_mqtt_connected = false;

std::string ctrl_topic(const char *suffix) {
  return std::string(kControllerBaseTopic) + "/" + suffix;
}

std::string display_topic(const char *suffix) {
  return std::string(kDisplayBaseTopic) + "/" + suffix;
}

// SimControllerTransport: bridges ControllerApp telemetry to MQTT topics
class SimControllerTransport : public thermostat::IControllerTransport {
 public:
  void publish_weather(float outdoor_temp_c, const char *condition) override {
    if (!g_mqtt_connected) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(outdoor_temp_c));
    g_mqtt.publish(ctrl_topic("state/outdoor_temp_c"), buf, true);
    g_mqtt.publish(ctrl_topic("state/outdoor_condition"),
                   condition != nullptr ? condition : "", true);
  }

  void publish_telemetry(const thermostat::ControllerTelemetry &t) override {
    if (!g_mqtt_connected) return;

    char buf[64];

    g_mqtt.publish(ctrl_topic("state/availability"), "online", true);

    const char *mode_str = "off";
    if (t.mode_code == 1) mode_str = "heat";
    else if (t.mode_code == 2) mode_str = "cool";
    g_mqtt.publish(ctrl_topic("state/mode"), mode_str, true);

    const char *fan_str = "auto";
    if (t.fan_code == 1) fan_str = "on";
    else if (t.fan_code == 2) fan_str = "circulate";
    g_mqtt.publish(ctrl_topic("state/fan_mode"), fan_str, true);

    snprintf(buf, sizeof(buf), "%.1f", t.setpoint_c);
    g_mqtt.publish(ctrl_topic("state/target_temp_c"), buf, true);

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(t.state));
    g_mqtt.publish(ctrl_topic("state/furnace_state"), buf, true);

    snprintf(buf, sizeof(buf), "%.1f", t.filter_runtime_hours);
    g_mqtt.publish(ctrl_topic("state/filter_runtime_hours"), buf, true);

    g_mqtt.publish(ctrl_topic("state/lockout"), t.lockout ? "1" : "0", true);

    snprintf(buf, sizeof(buf), "%u", t.seq);
    g_mqtt.publish(ctrl_topic("state/telemetry_seq"), buf, true);
  }
};

thermostat::ControllerConfig g_config;
SimControllerTransport g_transport;
thermostat::ControllerApp *g_app = nullptr;

uint32_t g_last_mqtt_connect_attempt_ms = 0;
bool g_mqtt_first_attempt = true;
uint32_t g_last_state_publish_ms = 0;

// MQTT primary-hold (matches esp32_controller_main.cpp)
constexpr uint32_t kMqttPrimaryHoldMs = 30000;
uint32_t g_last_mqtt_command_ms = 0;
bool g_espnow_command_enabled = true;

// Relay IO interlock layer
thermostat::ControllerRelayIo g_relay_io;

// Simulated state
float g_simulated_indoor_c = 20.0f;
uint32_t g_start_time_ms = 0;

// Simulated weather
float g_simulated_outdoor_c = 15.0f;
constexpr const char *kWeatherConditions[] = {
    "Sunny", "Cloudy", "Rain", "Snow", "Fog", "Lightning"};
constexpr int kWeatherConditionCount = 6;
int g_weather_index = 1;  // Start at Cloudy
bool g_weather_enabled = false;

// Real weather polling via PirateWeather API
constexpr uint32_t kWeatherPollMs = 15UL * 60UL * 1000UL;  // 15 minutes
std::string g_pirateweather_api_key;
std::string g_pirateweather_zip;
uint32_t g_last_weather_poll_ms = 0;
bool g_weather_api_configured = false;

const char *mode_name(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Off: return "OFF";
    case FurnaceMode::Heat: return "HEAT";
    case FurnaceMode::Cool: return "COOL";
    default: return "???";
  }
}

const char *mode_mqtt(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Off: return "off";
    case FurnaceMode::Heat: return "heat";
    case FurnaceMode::Cool: return "cool";
    default: return "off";
  }
}

const char *fan_mode_name(FanMode mode) {
  switch (mode) {
    case FanMode::Automatic: return "AUTO";
    case FanMode::AlwaysOn: return "ON";
    case FanMode::Circulate: return "CIRC";
    default: return "???";
  }
}

const char *fan_mode_mqtt(FanMode mode) {
  switch (mode) {
    case FanMode::Automatic: return "auto";
    case FanMode::AlwaysOn: return "on";
    case FanMode::Circulate: return "circulate";
    default: return "auto";
  }
}

const char *hvac_state_name(FurnaceStateCode state) {
  switch (state) {
    case FurnaceStateCode::Idle: return "Idle";
    case FurnaceStateCode::HeatMode: return "Heat Standby";
    case FurnaceStateCode::HeatOn: return "HEATING";
    case FurnaceStateCode::CoolMode: return "Cool Standby";
    case FurnaceStateCode::CoolOn: return "COOLING";
    case FurnaceStateCode::FanOn: return "Fan Running";
    case FurnaceStateCode::Error: return "ERROR";
    default: return "Unknown";
  }
}

void load_dotenv() {
  std::ifstream file(".env");
  if (!file.is_open()) return;

  std::string line;
  while (std::getline(file, line)) {
    // Skip empty lines and comments
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos || line[start] == '#') continue;

    size_t eq = line.find('=', start);
    if (eq == std::string::npos) continue;

    std::string key = line.substr(start, eq - start);
    std::string value = line.substr(eq + 1);

    // Trim trailing whitespace from key
    size_t end = key.find_last_not_of(" \t");
    if (end != std::string::npos) key.erase(end + 1);

    // Trim leading/trailing whitespace and optional quotes from value
    start = value.find_first_not_of(" \t");
    if (start != std::string::npos) value = value.substr(start);
    end = value.find_last_not_of(" \t\r\n");
    if (end != std::string::npos) value.erase(end + 1);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }

    // Don't overwrite existing env vars (0 = no overwrite)
    setenv(key.c_str(), value.c_str(), 0);
  }
}

void publish_controller_extras() {
  // Publish relay/uptime/failsafe info that the telemetry transport doesn't cover
  if (!g_mqtt_connected || g_app == nullptr) return;

  const auto &rt = g_app->runtime();
  char buf[64];

  g_mqtt.publish(ctrl_topic("state/relay_heat"), rt.heat_demand() ? "1" : "0", true);
  g_mqtt.publish(ctrl_topic("state/relay_cool"), rt.cool_demand() ? "1" : "0", true);
  g_mqtt.publish(ctrl_topic("state/relay_fan"), rt.fan_demand() ? "1" : "0", true);

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>((SDL_GetTicks() - g_start_time_ms) / 1000));
  g_mqtt.publish(ctrl_topic("state/uptime_s"), buf, true);

  g_mqtt.publish(ctrl_topic("state/failsafe"), rt.failsafe_active() ? "1" : "0", true);
}

void on_mqtt_message(const std::string &topic, const std::string &payload) {
  const uint32_t now = SDL_GetTicks();

  // Stamp MQTT command time for primary-hold logic
  if (topic.find("cmd/") != std::string::npos ||
      topic.find("packed_command") != std::string::npos) {
    g_last_mqtt_command_ms = now;
  }

  // Handle display's packed command (primary communication path)
  if (topic == display_topic("state/packed_command")) {
    uint32_t packed = static_cast<uint32_t>(strtoul(payload.c_str(), nullptr, 10));
    CommandWord cmd = espnow_cmd::decode(packed);

    printf("[MQTT RX] packed=%lu seq=%u mode=%d fan=%d setpoint=%.1f\n",
           static_cast<unsigned long>(packed), cmd.seq, static_cast<int>(cmd.mode),
           static_cast<int>(cmd.fan), cmd.setpoint_decic / 10.0f);

    auto result = g_app->on_command_word(packed);
    printf("[MQTT RX] Command %s\n", result.accepted ? "ACCEPTED" : "REJECTED");

    g_app->on_heartbeat(now);
    publish_controller_extras();
    return;
  }

  // Direct controller commands — build packed command word and route through app
  const auto &rt = g_app->runtime();

  if (topic == ctrl_topic("cmd/lockout")) {
    bool lockout = (payload == "1" || payload == "true" || payload == "on");
    g_app->set_hvac_lockout(lockout);
    publish_controller_extras();
    return;
  }

  if (topic == ctrl_topic("cmd/mode")) {
    FurnaceMode mode = FurnaceMode::Off;
    if (payload == "heat") mode = FurnaceMode::Heat;
    else if (payload == "cool") mode = FurnaceMode::Cool;

    uint32_t packed = thermostat::build_packed_command(
        mode, rt.fan_mode(), rt.target_temperature_c(),
        static_cast<uint16_t>((now / 100) & 0x1FF), false, false);
    g_app->on_command_word(packed);
    g_app->on_heartbeat(now);
    publish_controller_extras();
    return;
  }

  if (topic == ctrl_topic("cmd/fan_mode")) {
    FanMode fan = FanMode::Automatic;
    if (payload == "on" || payload == "always on") fan = FanMode::AlwaysOn;
    else if (payload == "circulate") fan = FanMode::Circulate;

    uint32_t packed = thermostat::build_packed_command(
        rt.mode(), fan, rt.target_temperature_c(),
        static_cast<uint16_t>((now / 100) & 0x1FF), false, false);
    g_app->on_command_word(packed);
    g_app->on_heartbeat(now);
    publish_controller_extras();
    return;
  }

  if (topic == ctrl_topic("cmd/target_temp_c")) {
    float temp = static_cast<float>(atof(payload.c_str()));

    uint32_t packed = thermostat::build_packed_command(
        rt.mode(), rt.fan_mode(), temp,
        static_cast<uint16_t>((now / 100) & 0x1FF), false, false);
    g_app->on_command_word(packed);
    g_app->on_heartbeat(now);
    publish_controller_extras();
    return;
  }

  if (topic == ctrl_topic("cmd/filter_reset")) {
    if (payload == "1" || payload == "true") {
      uint32_t packed = thermostat::build_packed_command(
          rt.mode(), rt.fan_mode(), rt.target_temperature_c(),
          static_cast<uint16_t>((now / 100) & 0x1FF), false, true);
      g_app->on_command_word(packed);
      g_app->on_heartbeat(now);
      publish_controller_extras();
    }
    return;
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

  // Clear any retained messages from previous sessions that could have stale sequence numbers
  g_mqtt.publish(display_topic("state/packed_command"), "", true);

  // Reset the sequence tracker so we accept the next command regardless of sequence
  g_app->reset_remote_command_sequence();

  g_mqtt.subscribe(ctrl_topic("cmd/lockout"));
  g_mqtt.subscribe(ctrl_topic("cmd/mode"));
  g_mqtt.subscribe(ctrl_topic("cmd/fan_mode"));
  g_mqtt.subscribe(ctrl_topic("cmd/target_temp_c"));
  g_mqtt.subscribe(ctrl_topic("cmd/filter_reset"));
  g_mqtt.subscribe(display_topic("state/packed_command"));

  publish_controller_extras();
}

SDL_Color to_sdl_color(uint32_t color) {
  return SDL_Color{
      static_cast<Uint8>((color >> 16) & 0xFF),
      static_cast<Uint8>((color >> 8) & 0xFF),
      static_cast<Uint8>(color & 0xFF),
      static_cast<Uint8>((color >> 24) & 0xFF)};
}

void set_color(uint32_t color) {
  SDL_Color c = to_sdl_color(color);
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
}

void draw_filled_rect(int x, int y, int w, int h, uint32_t color) {
  set_color(color);
  SDL_Rect rect = {x, y, w, h};
  SDL_RenderFillRect(g_renderer, &rect);
}

void draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
  set_color(color);
  SDL_Rect rect = {x, y, w, h};
  SDL_RenderDrawRect(g_renderer, &rect);
}

void draw_text(TTF_Font *font, int x, int y, const char *text, uint32_t color) {
  if (!font || !text || text[0] == '\0') return;

  SDL_Color c = to_sdl_color(color);
  SDL_Surface *surface = TTF_RenderText_Blended(font, text, c);
  if (!surface) return;

  SDL_Texture *texture = SDL_CreateTextureFromSurface(g_renderer, surface);
  if (texture) {
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(g_renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
  }
  SDL_FreeSurface(surface);
}

void draw_text_centered(TTF_Font *font, int cx, int cy, const char *text, uint32_t color) {
  if (!font || !text || text[0] == '\0') return;

  int w, h;
  TTF_SizeText(font, text, &w, &h);
  draw_text(font, cx - w / 2, cy - h / 2, text, color);
}

void draw_relay_block(int x, int y, const char *label, bool is_on, bool is_blocked) {
  const int relay_width = 80;
  const int relay_height = 80;

  uint32_t relay_color = kColorRelayOff;
  if (is_on) relay_color = kColorRelayOn;
  else if (is_blocked) relay_color = kColorRelayBlocked;

  // Relay body
  draw_filled_rect(x, y, relay_width, relay_height, relay_color);
  draw_rect_outline(x, y, relay_width, relay_height, kColorWhite);

  // LED indicator
  const int led_size = 10;
  const int led_x = x + relay_width / 2 - led_size / 2;
  const int led_y = y + relay_height - led_size - 10;
  const uint32_t led_color = is_on ? 0xFFFF0000 : 0xFF330000;
  draw_filled_rect(led_x, led_y, led_size, led_size, led_color);

  // Label
  draw_text_centered(g_font_medium, x + relay_width / 2, y + 25, label, kColorWhite);

  // Status text
  const char *status = is_on ? "ON" : (is_blocked ? "WAIT" : "OFF");
  uint32_t status_color = is_on ? kColorGreen : (is_blocked ? kColorYellow : 0xFF888888);
  draw_text_centered(g_font_small, x + relay_width / 2, y + relay_height - 25, status, status_color);
}

void draw_esp32_module(int x, int y) {
  const int module_width = 80;
  const int module_height = 100;

  draw_filled_rect(x, y, module_width, module_height, kColorEsp32Gray);
  draw_rect_outline(x, y, module_width, module_height, kColorWhite);

  // Antenna area
  draw_filled_rect(x + 10, y + 5, module_width - 20, 25, 0xFF222222);
  draw_rect_outline(x + 10, y + 5, module_width - 20, 25, 0xFF444444);

  // Label
  draw_text_centered(g_font_small, x + module_width / 2, y + 50, "ESP32", kColorLightBlue);
  draw_text_centered(g_font_small, x + module_width / 2, y + 70, "WROOM", kColorLightBlue);
}

void draw_power_section(int x, int y) {
  // Transformer
  draw_filled_rect(x, y, 50, 40, kColorPowerYellow);
  draw_rect_outline(x, y, 50, 40, 0xFF886600);
  draw_text_centered(g_font_small, x + 25, y + 20, "XFMR", 0xFF000000);

  // Terminal blocks
  draw_filled_rect(x + 60, y, 100, 30, 0xFF333333);
  draw_rect_outline(x + 60, y, 100, 30, kColorWhite);
  draw_text(g_font_small, x + 65, y + 8, "7-30V GND 5V", kColorWhite);
}

void draw_diagnostics_panel(int x, int y, int w, int h) {
  draw_filled_rect(x, y, w, h, kColorPanelBg);
  draw_rect_outline(x, y, w, h, kColorLightBlue);

  // Header
  draw_filled_rect(x + 2, y + 2, w - 4, 24, kColorHeaderBg);
  draw_text(g_font_medium, x + 10, y + 5, "SYSTEM STATUS", kColorLightBlue);

  const int line_height = 20;
  int ly = y + 35;
  char buf[128];

  // Mode & Fan
  snprintf(buf, sizeof(buf), "Mode: %s", mode_name(g_app->runtime().mode()));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  snprintf(buf, sizeof(buf), "Fan: %s", fan_mode_name(g_app->runtime().fan_mode()));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  // Temperatures
  snprintf(buf, sizeof(buf), "Setpoint: %.1f C", g_app->runtime().target_temperature_c());
  draw_text(g_font_small, x + 10, ly, buf, kColorYellow);
  ly += line_height;

  snprintf(buf, sizeof(buf), "Indoor: %.1f C", g_simulated_indoor_c);
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  // Deadband visualization
  float setpoint = g_app->runtime().target_temperature_c();
  float deadband = 0.5f;
  snprintf(buf, sizeof(buf), "Heat < %.1f C", setpoint - deadband);
  draw_text(g_font_small, x + 10, ly, buf, 0xFFFF8888);
  ly += line_height;

  snprintf(buf, sizeof(buf), "Cool > %.1f C", setpoint + deadband);
  draw_text(g_font_small, x + 10, ly, buf, 0xFF88CCFF);
  ly += line_height;

  // State
  ly += 5;
  FurnaceStateCode state = g_app->runtime().furnace_state();
  uint32_t state_color = kColorWhite;
  if (state == FurnaceStateCode::HeatOn) state_color = kColorRed;
  else if (state == FurnaceStateCode::CoolOn) state_color = kColorLightBlue;
  else if (state == FurnaceStateCode::FanOn) state_color = kColorGreen;
  else if (state == FurnaceStateCode::Error) state_color = kColorRed;

  snprintf(buf, sizeof(buf), "State: %s", hvac_state_name(state));
  draw_text(g_font_small, x + 10, ly, buf, state_color);
  ly += line_height;

  // Safety states
  ly += 5;
  snprintf(buf, sizeof(buf), "Failsafe: %s", g_app->runtime().failsafe_active() ? "ACTIVE" : "OK");
  draw_text(g_font_small, x + 10, ly, buf,
            g_app->runtime().failsafe_active() ? kColorRed : kColorGreen);
  ly += line_height;

  snprintf(buf, sizeof(buf), "Lockout: %s", g_app->runtime().hvac_lockout() ? "ACTIVE" : "OK");
  draw_text(g_font_small, x + 10, ly, buf,
            g_app->runtime().hvac_lockout() ? kColorRed : kColorGreen);
  ly += line_height;

  // Filter runtime
  snprintf(buf, sizeof(buf), "Filter: %.1f hrs", g_app->runtime().filter_runtime_hours());
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  // Weather
  ly += 5;
  if (g_weather_enabled) {
    const char *cond = g_app->has_outdoor_weather()
                           ? g_app->outdoor_condition()
                           : kWeatherConditions[g_weather_index];
    snprintf(buf, sizeof(buf), "Weather: %.1f C, %s",
             static_cast<double>(g_simulated_outdoor_c), cond);
    draw_text(g_font_small, x + 10, ly, buf, kColorLightBlue);
  } else {
    draw_text(g_font_small, x + 10, ly, "Weather: (disabled)", 0xFF888888);
  }
  ly += line_height;

  // MQTT status
  ly += 5;
  snprintf(buf, sizeof(buf), "MQTT: %s", g_mqtt_connected ? "Connected" : "Disconnected");
  draw_text(g_font_small, x + 10, ly, buf,
            g_mqtt_connected ? kColorGreen : kColorRed);
  ly += line_height;

  // MQTT primary-hold / ESP-NOW status
  const uint32_t diag_now = SDL_GetTicks();
  const bool mqtt_primary_active =
      g_mqtt_connected &&
      (static_cast<uint32_t>(diag_now - g_last_mqtt_command_ms) < kMqttPrimaryHoldMs);
  if (mqtt_primary_active) {
    uint32_t remaining_s = (kMqttPrimaryHoldMs -
        (diag_now - g_last_mqtt_command_ms)) / 1000;
    snprintf(buf, sizeof(buf), "MQTT Primary: ACTIVE (%lus)", static_cast<unsigned long>(remaining_s));
    draw_text(g_font_small, x + 10, ly, buf, kColorYellow);
  } else {
    draw_text(g_font_small, x + 10, ly, "ESP-NOW: enabled", kColorGreen);
  }
}

void draw_timing_panel(int x, int y, int w, int h) {
  draw_filled_rect(x, y, w, h, kColorPanelBg);
  draw_rect_outline(x, y, w, h, kColorLightBlue);

  // Header
  draw_filled_rect(x + 2, y + 2, w - 4, 24, kColorHeaderBg);
  draw_text(g_font_medium, x + 10, y + 5, "SAFETY INTERLOCKS", kColorLightBlue);

  const int line_height = 18;
  int ly = y + 35;
  char buf[128];
  const uint32_t now = SDL_GetTicks();

  // Show configured timings
  draw_text(g_font_small, x + 10, ly, "Min Run Times:", kColorYellow);
  ly += line_height;

  snprintf(buf, sizeof(buf), "  Heat: %lu sec",
           static_cast<unsigned long>(g_config.min_heating_run_time_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  snprintf(buf, sizeof(buf), "  Cool: %lu sec",
           static_cast<unsigned long>(g_config.min_cooling_run_time_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  ly += 5;
  draw_text(g_font_small, x + 10, ly, "Min Off Times:", kColorYellow);
  ly += line_height;

  snprintf(buf, sizeof(buf), "  Heat: %lu sec",
           static_cast<unsigned long>(g_config.min_heating_off_time_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  snprintf(buf, sizeof(buf), "  Cool: %lu sec",
           static_cast<unsigned long>(g_config.min_cooling_off_time_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  snprintf(buf, sizeof(buf), "  Idle: %lu sec",
           static_cast<unsigned long>(g_config.min_idle_time_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  // Heartbeat
  ly += 5;
  uint32_t hb_ago = now - g_app->runtime().heartbeat_last_seen_ms();
  snprintf(buf, sizeof(buf), "Heartbeat: %lu ms ago", static_cast<unsigned long>(hb_ago));
  uint32_t hb_color = (hb_ago < 5000) ? kColorGreen : ((hb_ago < 10000) ? kColorYellow : kColorRed);
  draw_text(g_font_small, x + 10, ly, buf, hb_color);
  ly += line_height;

  // Failsafe timeout
  snprintf(buf, sizeof(buf), "Failsafe timeout: %lu sec",
           static_cast<unsigned long>(g_config.failsafe_timeout_ms / 1000));
  draw_text(g_font_small, x + 10, ly, buf, kColorWhite);
  ly += line_height;

  // Relay IO interlock status
  ly += 5;
  draw_text(g_font_small, x + 10, ly, "Relay IO Interlock:", kColorYellow);
  ly += line_height;
  if (g_relay_io.has_pending()) {
    uint32_t remaining = g_relay_io.pending_wait_remaining_ms(now);
    snprintf(buf, sizeof(buf), "  Pending: %s (%lu ms)",
             g_relay_io.pending_name(), static_cast<unsigned long>(remaining));
    draw_text(g_font_small, x + 10, ly, buf, kColorRed);
  } else {
    draw_text(g_font_small, x + 10, ly, "  Clear", kColorGreen);
  }
}

void draw_controls_panel(int x, int y, int w, int h) {
  draw_filled_rect(x, y, w, h, kColorPanelBg);
  draw_rect_outline(x, y, w, h, kColorLightBlue);

  draw_filled_rect(x + 2, y + 2, w - 4, 24, kColorHeaderBg);
  draw_text(g_font_medium, x + 10, y + 5, "KEYBOARD", kColorLightBlue);

  const int line_height = 16;
  int ly = y + 35;

  draw_text(g_font_small, x + 10, ly, "H/C/O - Mode  F - Fan", kColorWhite); ly += line_height;
  draw_text(g_font_small, x + 10, ly, "+/- Setpoint  L - Lock", kColorWhite); ly += line_height;
  draw_text(g_font_small, x + 10, ly, "W - Weather  [/] - Temp", kColorWhite); ly += line_height;
  draw_text(g_font_small, x + 10, ly, "Q - Quit", kColorWhite); ly += line_height;
  if (!g_espnow_command_enabled) {
    draw_text(g_font_small, x + 10, ly, "MQTT hold: keys blocked", kColorYellow);
  }
}

void draw_header() {
  draw_filled_rect(0, 0, kDisplayWidth, 40, kColorHeaderBg);
  draw_rect_outline(0, 0, kDisplayWidth, 40, kColorLightBlue);
  draw_text(g_font_large, 15, 8, "ESP32 Furnace Controller Simulator", kColorWhite);
}

void render_board() {
  set_color(kColorPcbBlue);
  SDL_RenderClear(g_renderer);

  draw_header();

  // ESP32 module (top-left)
  draw_esp32_module(20, 55);

  // Power section
  draw_power_section(120, 60);

  // Diagnostics panel (right side)
  draw_diagnostics_panel(450, 50, 235, 250);

  // Timing panel
  draw_timing_panel(450, 310, 235, 180);

  // Relay section
  const int relay_y = 320;
  const int relay_start_x = 30;
  const int relay_spacing = 100;

  // Relay panel background
  draw_filled_rect(20, 280, 420, 130, kColorPanelBg);
  draw_rect_outline(20, 280, 420, 130, kColorLightBlue);
  draw_text(g_font_medium, 30, 285, "RELAY OUTPUTS (IO layer)", kColorLightBlue);

  // Use relay IO layer's latched output (what would actually reach GPIO)
  const auto &relay_out = g_relay_io.latched_output();
  bool io_pending = g_relay_io.has_pending();

  // A relay is "blocked" if the runtime demands it but the IO layer hasn't engaged it yet
  bool heat_blocked = g_app->runtime().heat_demand() && !relay_out.heat && io_pending;
  bool cool_blocked = g_app->runtime().cool_demand() && !relay_out.cool && io_pending;
  bool fan_blocked = g_app->runtime().fan_demand() && !relay_out.fan && io_pending;

  draw_relay_block(relay_start_x, relay_y, "HEAT", relay_out.heat, heat_blocked);
  draw_relay_block(relay_start_x + relay_spacing, relay_y, "COOL", relay_out.cool, cool_blocked);
  draw_relay_block(relay_start_x + relay_spacing * 2, relay_y, "FAN", relay_out.fan, fan_blocked);
  draw_relay_block(relay_start_x + relay_spacing * 3, relay_y, "SPARE", relay_out.spare, false);

  // Controls panel
  draw_controls_panel(20, 420, 200, 70);

  // Uptime
  char buf[64];
  snprintf(buf, sizeof(buf), "Uptime: %lu sec",
           static_cast<unsigned long>((SDL_GetTicks() - g_start_time_ms) / 1000));
  draw_text(g_font_small, 250, 450, buf, kColorWhite);
}

void send_simulated_command(FurnaceMode mode, FanMode fan, float setpoint) {
  if (!g_espnow_command_enabled) {
    uint32_t remaining = kMqttPrimaryHoldMs -
        (SDL_GetTicks() - g_last_mqtt_command_ms);
    printf("[ESP-NOW] Command BLOCKED — MQTT primary hold active (%lu s remaining)\n",
           static_cast<unsigned long>(remaining / 1000));
    return;
  }

  static uint16_t seq = 0;
  ++seq;

  uint32_t packed = thermostat::build_packed_command(mode, fan, setpoint, seq, false, false);
  g_app->on_command_word(packed);
  g_app->on_heartbeat(SDL_GetTicks());
  publish_controller_extras();
}

void handle_keypress(SDL_Keycode key) {
  switch (key) {
    case SDLK_h:
      send_simulated_command(FurnaceMode::Heat, g_app->runtime().fan_mode(),
                             g_app->runtime().target_temperature_c());
      break;

    case SDLK_c:
      send_simulated_command(FurnaceMode::Cool, g_app->runtime().fan_mode(),
                             g_app->runtime().target_temperature_c());
      break;

    case SDLK_o:
      send_simulated_command(FurnaceMode::Off, g_app->runtime().fan_mode(),
                             g_app->runtime().target_temperature_c());
      break;

    case SDLK_f: {
      FanMode next_fan;
      switch (g_app->runtime().fan_mode()) {
        case FanMode::Automatic: next_fan = FanMode::AlwaysOn; break;
        case FanMode::AlwaysOn: next_fan = FanMode::Circulate; break;
        default: next_fan = FanMode::Automatic; break;
      }
      send_simulated_command(g_app->runtime().mode(), next_fan, g_app->runtime().target_temperature_c());
      break;
    }

    case SDLK_PLUS:
    case SDLK_EQUALS:
      send_simulated_command(g_app->runtime().mode(), g_app->runtime().fan_mode(),
                             g_app->runtime().target_temperature_c() + 0.5f);
      break;

    case SDLK_MINUS:
      send_simulated_command(g_app->runtime().mode(), g_app->runtime().fan_mode(),
                             g_app->runtime().target_temperature_c() - 0.5f);
      break;

    case SDLK_t:
      g_app->on_heartbeat(SDL_GetTicks());
      break;

    case SDLK_l:
      g_app->set_hvac_lockout(!g_app->runtime().hvac_lockout());
      publish_controller_extras();
      break;

    case SDLK_w: {
      g_weather_enabled = true;
      g_weather_index = (g_weather_index + 1) % kWeatherConditionCount;
      g_app->set_outdoor_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      g_transport.publish_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      printf("[SIM] Weather: %s, Outdoor: %.1f C\n",
             kWeatherConditions[g_weather_index], static_cast<double>(g_simulated_outdoor_c));
      break;
    }

    case SDLK_RIGHTBRACKET:
      g_weather_enabled = true;
      g_simulated_outdoor_c += 2.0f;
      if (g_simulated_outdoor_c > 50.0f) g_simulated_outdoor_c = 50.0f;
      g_app->set_outdoor_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      g_transport.publish_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      printf("[SIM] Outdoor temp: %.1f C\n", static_cast<double>(g_simulated_outdoor_c));
      break;

    case SDLK_LEFTBRACKET:
      g_weather_enabled = true;
      g_simulated_outdoor_c -= 2.0f;
      if (g_simulated_outdoor_c < -40.0f) g_simulated_outdoor_c = -40.0f;
      g_app->set_outdoor_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      g_transport.publish_weather(g_simulated_outdoor_c, kWeatherConditions[g_weather_index]);
      printf("[SIM] Outdoor temp: %.1f C\n", static_cast<double>(g_simulated_outdoor_c));
      break;

    case SDLK_q:
    case SDLK_ESCAPE:
      g_running = false;
      break;
  }
}

void process_events() {
  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      g_running = false;
      return;
    }
    if (e.type == SDL_KEYDOWN) {
      handle_keypress(e.key.keysym.sym);
    }
  }
}

void tick_controller() {
  const uint32_t now = SDL_GetTicks();

  // Evaluate MQTT primary-hold (matches esp32_controller_main.cpp)
  const bool mqtt_primary_active =
      g_mqtt_connected &&
      (static_cast<uint32_t>(now - g_last_mqtt_command_ms) < kMqttPrimaryHoldMs);
  g_espnow_command_enabled = !mqtt_primary_active;

  // Feed simulated indoor temperature into the app — ControllerApp::tick()
  // uses compute_hvac_calls() with proper deadband/overrun logic.
  g_app->on_indoor_temperature_c(g_simulated_indoor_c);
  g_app->tick(now);

  // Apply relay IO interlock layer on top of runtime demand
  auto snap = g_app->runtime().snapshot();
  g_relay_io.apply(now, snap.relay, snap.failsafe_active || snap.hvac_lockout);

  // Simulate temperature drift based on actual relay output (post-interlock)
  const auto &relay_out = g_relay_io.latched_output();
  if (relay_out.heat) {
    g_simulated_indoor_c += 0.005f;  // Faster heating for demo
  } else if (relay_out.cool) {
    g_simulated_indoor_c -= 0.005f;  // Faster cooling for demo
  } else {
    const float outdoor = 15.0f;
    g_simulated_indoor_c += (outdoor - g_simulated_indoor_c) * 0.0002f;
  }

  if (g_simulated_indoor_c < 5.0f) g_simulated_indoor_c = 5.0f;
  if (g_simulated_indoor_c > 40.0f) g_simulated_indoor_c = 40.0f;

  // Publish relay/uptime extras periodically
  if (g_mqtt_connected && (now - g_last_state_publish_ms) > 5000) {
    g_last_state_publish_ms = now;
    publish_controller_extras();
  }
}

bool init_sdl() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  if (TTF_Init() != 0) {
    fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
    return false;
  }

  g_window = SDL_CreateWindow("ESP32 Controller Simulator", SDL_WINDOWPOS_CENTERED,
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

  // Load fonts - try common system font paths
  const char *font_paths[] = {
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    nullptr
  };

  for (int i = 0; font_paths[i] != nullptr; ++i) {
    g_font_large = TTF_OpenFont(font_paths[i], 20);
    if (g_font_large) {
      g_font_medium = TTF_OpenFont(font_paths[i], 14);
      g_font_small = TTF_OpenFont(font_paths[i], 12);
      break;
    }
  }

  if (!g_font_large) {
    fprintf(stderr, "Warning: Could not load any fonts, text will not display\n");
  }

  return true;
}

void shutdown() {
  if (g_mqtt_connected) {
    g_mqtt.publish(ctrl_topic("state/availability"), "offline", true);
    g_mqtt.disconnect();
  }

  if (g_font_small) TTF_CloseFont(g_font_small);
  if (g_font_medium) TTF_CloseFont(g_font_medium);
  if (g_font_large) TTF_CloseFont(g_font_large);

  TTF_Quit();

  if (g_renderer != nullptr) {
    SDL_DestroyRenderer(g_renderer);
    g_renderer = nullptr;
  }
  if (g_window != nullptr) {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }
  SDL_Quit();

  delete g_app;
  g_app = nullptr;
}

void poll_weather() {
  if (!g_weather_api_configured || g_app == nullptr) return;

  const uint32_t now = SDL_GetTicks();
  if (g_last_weather_poll_ms != 0 &&
      (now - g_last_weather_poll_ms) < kWeatherPollMs) {
    return;
  }
  g_last_weather_poll_ms = now;

  printf("[Weather] Fetching weather for ZIP %s...\n", g_pirateweather_zip.c_str());

  auto result = sim::fetch_weather(g_pirateweather_api_key, g_pirateweather_zip);
  if (!result.ok) {
    printf("[Weather] Fetch failed\n");
    return;
  }

  printf("[Weather] %.1f C, %s\n",
         static_cast<double>(result.temp_c), result.condition.c_str());

  // Update simulator state
  g_weather_enabled = true;
  g_simulated_outdoor_c = result.temp_c;

  // Find matching condition index for diagnostics panel display
  for (int i = 0; i < kWeatherConditionCount; ++i) {
    if (result.condition == kWeatherConditions[i]) {
      g_weather_index = i;
      break;
    }
  }

  // Push into app and publish via MQTT
  g_app->set_outdoor_weather(result.temp_c, result.condition.c_str());
  g_transport.publish_weather(result.temp_c, result.condition.c_str());
}

}  // namespace

int main(int /* argc */, char ** /* argv */) {
  if (!init_sdl()) {
    shutdown();
    return 1;
  }

  // Initialize with reasonable simulation timings
  g_config.failsafe_timeout_ms = 30000;       // 30 seconds
  g_config.min_heating_off_time_ms = 10000;   // 10 seconds
  g_config.min_cooling_off_time_ms = 10000;   // 10 seconds
  g_config.min_heating_run_time_ms = 5000;    // 5 seconds
  g_config.min_cooling_run_time_ms = 5000;    // 5 seconds
  g_config.min_idle_time_ms = 3000;           // 3 seconds

  g_app = new thermostat::ControllerApp(g_transport, g_config);
  g_relay_io.begin();
  g_start_time_ms = SDL_GetTicks();

  g_app->on_heartbeat(SDL_GetTicks());

  g_mqtt.set_message_callback(on_mqtt_message);

  load_dotenv();

  // Check for PirateWeather API credentials
  const char *pw_key = getenv("PIRATEWEATHER_API_KEY");
  const char *pw_zip = getenv("PIRATEWEATHER_ZIP");
  if (pw_key != nullptr && pw_key[0] != '\0' &&
      pw_zip != nullptr && pw_zip[0] != '\0') {
    g_pirateweather_api_key = pw_key;
    g_pirateweather_zip = pw_zip;
    g_weather_api_configured = true;
    printf("PirateWeather API configured (ZIP: %s)\n", pw_zip);
  } else {
    printf("PirateWeather not configured (set PIRATEWEATHER_API_KEY and PIRATEWEATHER_ZIP)\n");
  }

  printf("ESP32 Controller Simulator\n");
  printf("Connecting to MQTT broker at %s:%d\n", kMqttHost, kMqttPort);

  while (g_running) {
    ensure_mqtt_connected();
    g_mqtt.loop();
    poll_weather();
    process_events();
    tick_controller();
    render_board();
    SDL_RenderPresent(g_renderer);
    SDL_Delay(16);
  }

  printf("\nShutting down...\n");
  shutdown();
  return 0;
}
