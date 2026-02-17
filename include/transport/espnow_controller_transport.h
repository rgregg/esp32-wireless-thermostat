#pragma once

#include <stdint.h>

#include "controller_app.h"

namespace thermostat {

struct EspNowControllerConfig {
  uint8_t peer_mac[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 6;
  bool encrypted = false;
  uint8_t lmk[16] = {0};
  uint32_t heartbeat_interval_ms = 10000;
};

using CommandWordCallback = void (*)(uint32_t packed_word, void *ctx);
using HeartbeatCallback = void (*)(uint32_t now_ms, void *ctx);
using IndoorValueCallback = void (*)(float value, void *ctx);

class EspNowControllerTransport final : public IControllerTransport {
 public:
  EspNowControllerTransport() = default;

  bool begin(const EspNowControllerConfig &config);
  void loop(uint32_t now_ms);

  void set_callbacks(CommandWordCallback command_cb,
                     HeartbeatCallback heartbeat_cb,
                     IndoorValueCallback indoor_temp_cb,
                     IndoorValueCallback indoor_humidity_cb,
                     void *callback_context);

  void publish_telemetry(const ControllerTelemetry &telemetry) override;

 private:
  static void on_recv_static(const void *recv_info, const uint8_t *data, int len);
  void on_recv(const uint8_t *data, int len);
  void send_heartbeat(uint32_t now_ms);

  EspNowControllerConfig config_{};
  bool initialized_ = false;
  uint32_t last_heartbeat_sent_ms_ = 0;
  bool heartbeat_toggle_ = false;

  CommandWordCallback command_cb_ = nullptr;
  HeartbeatCallback heartbeat_cb_ = nullptr;
  IndoorValueCallback indoor_temp_cb_ = nullptr;
  IndoorValueCallback indoor_humidity_cb_ = nullptr;
  void *callback_context_ = nullptr;

  static EspNowControllerTransport *instance_;
};

}  // namespace thermostat
