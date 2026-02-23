#pragma once

#include <stdint.h>

#include "controller/controller_app.h"
#include "controller/transport/espnow_controller_transport.h"

namespace thermostat {

class ControllerNode {
 public:
  ControllerNode(const ControllerConfig &controller_config,
                 const EspNowControllerConfig &transport_config);

  bool begin();

  // Runs transport loop and app tick using internally computed HVAC calls.
  void tick(uint32_t now_ms);
  void set_espnow_command_enabled(bool enabled) { espnow_command_enabled_ = enabled; }

  ControllerApp &app() { return app_; }
  EspNowControllerTransport &transport() { return transport_; }

 private:
  static void on_command_word_static(uint32_t packed_word, const uint8_t *src_mac,
                                     void *ctx);
  static void on_heartbeat_static(uint32_t now_ms, void *ctx);
  static void on_indoor_temp_static(float value, void *ctx);
  static void on_indoor_humidity_static(float value, void *ctx);
  static void on_thermostat_ack_static(uint16_t seq, void *ctx);

  void on_command_word(uint32_t packed_word, const uint8_t *src_mac);
  void on_heartbeat(uint32_t now_ms);
  void on_indoor_temp(float value);
  void on_indoor_humidity(float value);
  void on_thermostat_ack(uint16_t seq);

  EspNowControllerTransport transport_;
  ControllerApp app_;
  EspNowControllerConfig transport_config_{};
  bool espnow_command_enabled_ = true;
};

}  // namespace thermostat
