#include "controller/controller_node.h"

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
  transport_.loop(now_ms);
  app_.tick(now_ms);
}

void ControllerNode::on_command_word_static(uint32_t packed_word,
                                             const uint8_t *src_mac, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->on_command_word(packed_word, src_mac);
}

void ControllerNode::on_heartbeat_static(uint32_t now_ms, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->on_heartbeat(now_ms);
}

void ControllerNode::on_indoor_temp_static(float value, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->on_indoor_temp(value);
}

void ControllerNode::on_indoor_humidity_static(float value, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->on_indoor_humidity(value);
}

void ControllerNode::on_thermostat_ack_static(uint16_t seq, void *ctx) {
  if (ctx == nullptr) {
    return;
  }
  static_cast<ControllerNode *>(ctx)->on_thermostat_ack(seq);
}

void ControllerNode::on_command_word(uint32_t packed_word, const uint8_t *src_mac) {
  if (!espnow_command_enabled_) {
    return;
  }
  app_.on_command_word(packed_word, src_mac);
}

void ControllerNode::on_heartbeat(uint32_t now_ms) {
  app_.on_heartbeat(now_ms);
}

void ControllerNode::on_indoor_temp(float value) {
  app_.on_indoor_temperature_c(value);
}

void ControllerNode::on_indoor_humidity(float value) {
  app_.on_indoor_humidity(value);
}

void ControllerNode::on_thermostat_ack(uint16_t seq) {
  app_.on_thermostat_ack(seq);
}

}  // namespace thermostat
