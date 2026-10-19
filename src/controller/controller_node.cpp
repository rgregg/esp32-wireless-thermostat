#include "controller/controller_node.h"

#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace thermostat {

ControllerNode::ControllerNode(const ControllerConfig &controller_config,
                               const EspNowControllerConfig &transport_config)
    : transport_(), app_(transport_, controller_config), transport_config_(transport_config) {}

bool ControllerNode::begin() {
  transport_.set_callbacks(&ControllerNode::on_command_word_static,
                           &ControllerNode::on_heartbeat_static,
                           &ControllerNode::on_indoor_temp_static,
                           &ControllerNode::on_indoor_humidity_static,
                           &ControllerNode::on_thermostat_ack_static,
                           this);
  return transport_.begin(transport_config_);
}

void ControllerNode::tick(uint32_t now_ms) {
  drain_inbound(now_ms);
  transport_.loop(now_ms);
  app_.tick(now_ms);
}

void ControllerNode::on_command_word_static(uint32_t packed_word,
                                             const uint8_t *src_mac, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->enqueue(InboundType::CommandWord, src_mac,
                                               packed_word, 0.0f);
}

void ControllerNode::on_heartbeat_static(uint32_t now_ms, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  (void)now_ms;  // applied at the loop's time (see apply())
  static_cast<ControllerNode *>(ctx)->enqueue(InboundType::Heartbeat, nullptr, 0, 0.0f);
}

void ControllerNode::on_indoor_temp_static(float value, const uint8_t *src_mac,
                                            void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->enqueue(InboundType::IndoorTemp, src_mac, 0, value);
}

void ControllerNode::on_indoor_humidity_static(float value, const uint8_t *src_mac,
                                                void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->enqueue(InboundType::IndoorHumidity, src_mac, 0,
                                               value);
}

void ControllerNode::on_thermostat_ack_static(uint16_t seq, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->enqueue(InboundType::Ack, nullptr, seq, 0.0f);
}

void ControllerNode::enqueue(InboundType type, const uint8_t *src_mac, uint32_t u32,
                             float value) {
  Inbound in;
  in.type = type;
  if (src_mac != nullptr) {
    in.has_mac = true;
    memcpy(in.mac, src_mac, sizeof(in.mac));
  }
  in.u32 = u32;
  in.value = value;
#if defined(ARDUINO)
  in.rx_ms = millis();
#endif
  if (!inbound_.push(in)) {
    inbound_dropped_.fetch_add(1);  // full: the display resends commands and heartbeats
  }
}

void ControllerNode::drain_inbound(uint32_t now_ms) {
  Inbound in;
  while (inbound_.pop(in)) {
    apply(in, now_ms);
  }
}

void ControllerNode::apply(const Inbound &in, uint32_t now_ms) {
  const uint8_t *mac = in.has_mac ? in.mac : nullptr;
  switch (in.type) {
    case InboundType::CommandWord:
      // Checked when applied: the loop sets this each pass from MQTT-primary state.
      if (in.rx_ms != 0) {
        const uint32_t waited = now_ms - in.rx_ms;
        // A frame stamped after the loop read now_ms is "in the future": it did not wait.
        if (waited < 0x80000000u && waited > command_wait_max_ms_) command_wait_max_ms_ = waited;
      }
      if (espnow_command_enabled_) {
        app_.on_command_word(in.u32, mac);
      } else {
        ++espnow_commands_gated_;
      }
      break;
    case InboundType::Heartbeat:
      // Stamped with this tick's time, not the WiFi task's receive time: a heartbeat
      // received after the loop read now_ms would be "in the future", and the failsafe's
      // now - last_seen would wrap and trip it for a tick.
      app_.on_heartbeat(now_ms);
      break;
    case InboundType::IndoorTemp:
      app_.on_indoor_temperature_c(in.value, mac);
      break;
    case InboundType::IndoorHumidity:
      app_.on_indoor_humidity(in.value, mac);
      break;
    case InboundType::Ack:
      app_.on_thermostat_ack(static_cast<uint16_t>(in.u32));
      break;
  }
}

}  // namespace thermostat
