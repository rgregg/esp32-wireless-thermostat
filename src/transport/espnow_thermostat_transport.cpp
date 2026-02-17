#include "transport/espnow_thermostat_transport.h"

#include <string.h>

#include "thermostat_types.h"

#if defined(ARDUINO)
#include <WiFi.h>
#include <esp_now.h>
#endif

namespace thermostat {

EspNowThermostatTransport *EspNowThermostatTransport::instance_ = nullptr;

bool EspNowThermostatTransport::begin(const EspNowThermostatConfig &config) {
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
      reinterpret_cast<esp_now_recv_cb_t>(&EspNowThermostatTransport::on_recv_static));

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

void EspNowThermostatTransport::loop(uint32_t now_ms) {
  if (!initialized_ || config_.heartbeat_interval_ms == 0) {
    return;
  }

  if (last_heartbeat_sent_ms_ == 0 ||
      static_cast<uint32_t>(now_ms - last_heartbeat_sent_ms_) >=
          config_.heartbeat_interval_ms) {
    send_heartbeat(now_ms);
  }
}

void EspNowThermostatTransport::set_callbacks(ThermostatHeartbeatCallback heartbeat_cb,
                                              ThermostatTelemetryCallback telemetry_cb,
                                              void *callback_context) {
  heartbeat_cb_ = heartbeat_cb;
  telemetry_cb_ = telemetry_cb;
  callback_context_ = callback_context;
}

void EspNowThermostatTransport::publish_command_word(uint32_t packed_word) {
  if (!initialized_) {
    return;
  }

  CommandWordPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::CommandWord);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.packed_word = packed_word;

#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#endif
}

void EspNowThermostatTransport::publish_indoor_temperature_c(float temp_c) {
  if (!initialized_) {
    return;
  }

  FloatValuePacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::IndoorTemperature);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.value = temp_c;

#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#endif
}

void EspNowThermostatTransport::publish_indoor_humidity(float humidity_pct) {
  if (!initialized_) {
    return;
  }

  FloatValuePacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::IndoorHumidity);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.value = humidity_pct;

#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#endif
}

void EspNowThermostatTransport::on_recv_static(const void * /*recv_info*/,
                                               const uint8_t *data,
                                               int len) {
  if (instance_ == nullptr) {
    return;
  }
  instance_->on_recv(data, len);
}

void EspNowThermostatTransport::on_recv(const uint8_t *data, int len) {
  if (data == nullptr || len < static_cast<int>(sizeof(PacketHeader))) {
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

    case PacketType::ControllerTelemetry:
      if (len >= static_cast<int>(sizeof(ControllerTelemetryPacket)) &&
          telemetry_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const ControllerTelemetryPacket *>(data);

        ThermostatControllerTelemetry telemetry;
        telemetry.state = static_cast<FurnaceStateCode>(pkt->state);
        telemetry.lockout = pkt->lockout != 0;
        telemetry.mode_code = pkt->mode;
        telemetry.fan_code = pkt->fan;
        telemetry.setpoint_c = static_cast<float>(pkt->setpoint_decic) / 10.0f;
        telemetry.filter_runtime_seconds = pkt->filter_runtime_seconds;

        telemetry_cb_(telemetry, callback_context_);
      }
      break;

    default:
      break;
  }
}

void EspNowThermostatTransport::send_heartbeat(uint32_t now_ms) {
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
