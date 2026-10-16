#pragma once

#include <stdint.h>

#include <atomic>

#include "controller/controller_app.h"
#include "controller/transport/espnow_controller_transport.h"
#include "spsc_ring.h"

namespace thermostat {

class ControllerNode {
 public:
  ControllerNode(const ControllerConfig &controller_config,
                 const EspNowControllerConfig &transport_config);

  bool begin();

  // Applies the ESP-NOW frames received since the last tick, then runs the transport
  // loop and the app tick using internally computed HVAC calls. Call from the loop task.
  void tick(uint32_t now_ms);
  // Frames dropped because the inbound queue was full (diagnostic).
  uint32_t inbound_dropped() const { return inbound_dropped_.load(); }
  void set_espnow_command_enabled(bool enabled) { espnow_command_enabled_ = enabled; }

  ControllerApp &app() { return app_; }
  EspNowControllerTransport &transport() { return transport_; }

 private:
  static void on_command_word_static(uint32_t packed_word, const uint8_t *src_mac,
                                     void *ctx);
  static void on_heartbeat_static(uint32_t now_ms, void *ctx);
  static void on_indoor_temp_static(float value, const uint8_t *src_mac, void *ctx);
  static void on_indoor_humidity_static(float value, const uint8_t *src_mac, void *ctx);
  static void on_thermostat_ack_static(uint16_t seq, void *ctx);

  // The transport's receive callbacks run on the WiFi task. They only queue the frame;
  // tick() applies it on the loop task, so ControllerApp state is never mutated
  // concurrently with its tick().
  enum class InboundType : uint8_t { CommandWord, Heartbeat, IndoorTemp, IndoorHumidity, Ack };
  struct Inbound {
    InboundType type = InboundType::Heartbeat;
    bool has_mac = false;
    uint8_t mac[6] = {};
    uint32_t u32 = 0;  // packed command word or ACK seq
    float value = 0.0f;
  };
  void enqueue(InboundType type, const uint8_t *src_mac, uint32_t u32, float value);
  void apply(const Inbound &in, uint32_t now_ms);
  void drain_inbound(uint32_t now_ms);

  EspNowControllerTransport transport_;
  ControllerApp app_;
  EspNowControllerConfig transport_config_{};
  bool espnow_command_enabled_ = true;
  SpscRing<Inbound, 16> inbound_;
  std::atomic<uint32_t> inbound_dropped_{0};
};

}  // namespace thermostat
