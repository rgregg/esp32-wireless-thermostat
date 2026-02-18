#include "transport/espnow_controller_transport.h"

#include <string.h>

#include "transport/espnow_packets.h"

#if defined(ARDUINO)
#include <WiFi.h>
#include <esp_now.h>
#endif

namespace thermostat {

namespace {
bool is_broadcast_mac(const uint8_t mac[6]) {
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, kBroadcast, sizeof(kBroadcast)) == 0;
}
}  // namespace

EspNowControllerTransport *EspNowControllerTransport::instance_ = nullptr;

bool EspNowControllerTransport::begin(const EspNowControllerConfig &config) {
  config_ = config;
  last_heartbeat_sent_ms_ = 0;
  heartbeat_toggle_ = false;

#if defined(ARDUINO)
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    initialized_ = false;
    return false;
  }

  esp_now_register_recv_cb(
      reinterpret_cast<esp_now_recv_cb_t>(&EspNowControllerTransport::on_recv_static));
  esp_now_register_send_cb(
      reinterpret_cast<esp_now_send_cb_t>(&EspNowControllerTransport::on_send_static));

  esp_now_peer_info_t peer_info;
  memset(&peer_info, 0, sizeof(peer_info));
  memcpy(peer_info.peer_addr, config_.peer_mac, sizeof(peer_info.peer_addr));
  peer_info.channel = config_.channel;
  peer_info.encrypt = config_.encrypted;
  if (peer_info.encrypt) {
    memcpy(peer_info.lmk, config_.lmk, sizeof(peer_info.lmk));
  }

  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    initialized_ = false;
    return false;
  }

  instance_ = this;
  initialized_ = true;
  return true;
#else
  (void)config_;
  initialized_ = true;
  return true;
#endif
}

void EspNowControllerTransport::loop(uint32_t now_ms) {
  if (!initialized_) {
    return;
  }

  if (config_.heartbeat_interval_ms == 0) {
    return;
  }

  if (last_heartbeat_sent_ms_ == 0 ||
      static_cast<uint32_t>(now_ms - last_heartbeat_sent_ms_) >=
          config_.heartbeat_interval_ms) {
    send_heartbeat(now_ms);
  }
}

void EspNowControllerTransport::set_callbacks(CommandWordCallback command_cb,
                                              HeartbeatCallback heartbeat_cb,
                                              IndoorValueCallback indoor_temp_cb,
                                              IndoorValueCallback indoor_humidity_cb,
                                              ThermostatAckCallback ack_cb,
                                              void *callback_context) {
  command_cb_ = command_cb;
  heartbeat_cb_ = heartbeat_cb;
  indoor_temp_cb_ = indoor_temp_cb;
  indoor_humidity_cb_ = indoor_humidity_cb;
  ack_cb_ = ack_cb;
  callback_context_ = callback_context;
}

void EspNowControllerTransport::publish_telemetry(
    const ControllerTelemetry &telemetry) {
  if (!initialized_) {
    return;
  }

  ControllerTelemetryPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::ControllerTelemetry);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.seq = telemetry.seq;
  pkt.state = static_cast<uint8_t>(telemetry.state);
  pkt.lockout = telemetry.lockout ? 1 : 0;
  pkt.mode = telemetry.mode_code;
  pkt.fan = telemetry.fan_code;

  int setpoint_decic = static_cast<int>(telemetry.setpoint_c * 10.0f);
  if (setpoint_decic < 0) {
    setpoint_decic = 0;
  }
  if (setpoint_decic > 400) {
    setpoint_decic = 400;
  }
  pkt.setpoint_decic = static_cast<uint16_t>(setpoint_decic);

  const float clamped_hours =
      telemetry.filter_runtime_hours < 0.0f ? 0.0f : telemetry.filter_runtime_hours;
  pkt.filter_runtime_seconds = static_cast<uint32_t>(clamped_hours * 3600.0f);

#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#endif
}

void EspNowControllerTransport::on_recv_static(const void *recv_info,
                                               const uint8_t *data,
                                               int len) {
  if (instance_ == nullptr) {
    return;
  }
  const uint8_t *src_mac = reinterpret_cast<const uint8_t *>(recv_info);
  instance_->on_recv(src_mac, data, len);
}

void EspNowControllerTransport::on_send_static(const uint8_t * /*mac_addr*/, int status) {
  if (instance_ == nullptr) {
    return;
  }
#if defined(ARDUINO)
  if (status == static_cast<int>(ESP_NOW_SEND_SUCCESS)) {
    ++instance_->send_ok_count_;
  } else {
    ++instance_->send_fail_count_;
  }
#else
  (void)status;
#endif
}

void EspNowControllerTransport::on_recv(const uint8_t *src_mac,
                                        const uint8_t *data,
                                        int len) {
  if (data == nullptr || len < static_cast<int>(sizeof(PacketHeader))) {
    return;
  }
  if (src_mac != nullptr && !is_broadcast_mac(config_.peer_mac) &&
      memcmp(src_mac, config_.peer_mac, 6) != 0) {
    return;
  }

  const auto *header = reinterpret_cast<const PacketHeader *>(data);
  if (header->version != kEspNowProtocolVersion) {
    return;
  }

  const auto packet_type = static_cast<PacketType>(header->type);
  switch (packet_type) {
    case PacketType::Heartbeat:
      if (len >= static_cast<int>(sizeof(HeartbeatPacket)) && heartbeat_cb_ != nullptr) {
#if defined(ARDUINO)
        heartbeat_cb_(millis(), callback_context_);
#else
        heartbeat_cb_(0, callback_context_);
#endif
      }
      break;

    case PacketType::CommandWord:
      if (len >= static_cast<int>(sizeof(CommandWordPacket)) && command_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const CommandWordPacket *>(data);
        command_cb_(pkt->packed_word, callback_context_);
      }
      break;

    case PacketType::IndoorTemperature:
      if (len >= static_cast<int>(sizeof(FloatValuePacket)) && indoor_temp_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const FloatValuePacket *>(data);
        indoor_temp_cb_(pkt->value, callback_context_);
      }
      break;

    case PacketType::IndoorHumidity:
      if (len >= static_cast<int>(sizeof(FloatValuePacket)) &&
          indoor_humidity_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const FloatValuePacket *>(data);
        indoor_humidity_cb_(pkt->value, callback_context_);
      }
      break;

    case PacketType::ControllerAck:
      if (len >= static_cast<int>(sizeof(ControllerAckPacket)) && ack_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const ControllerAckPacket *>(data);
        ack_cb_(pkt->seq, callback_context_);
      }
      break;

    default:
      break;
  }
}

void EspNowControllerTransport::send_heartbeat(uint32_t now_ms) {
  last_heartbeat_sent_ms_ = now_ms;
  heartbeat_toggle_ = !heartbeat_toggle_;

  HeartbeatPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::Heartbeat);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.toggle = heartbeat_toggle_ ? 1 : 0;

#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#endif
}

}  // namespace thermostat
