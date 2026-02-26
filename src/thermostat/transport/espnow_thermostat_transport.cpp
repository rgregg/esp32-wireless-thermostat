#include "thermostat/transport/espnow_thermostat_transport.h"

#include <string.h>

#include "thermostat_types.h"

#if defined(ARDUINO)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

namespace thermostat {

namespace {
bool is_broadcast_mac(const uint8_t mac[6]) {
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return memcmp(mac, kBroadcast, sizeof(kBroadcast)) == 0;
}
}  // namespace

EspNowThermostatTransport *EspNowThermostatTransport::instance_ = nullptr;

bool EspNowThermostatTransport::begin(const EspNowThermostatConfig &config) {
  config_ = config;
  last_heartbeat_sent_ms_ = 0;
  heartbeat_toggle_ = false;

#if defined(ARDUINO)
  WiFi.mode(WIFI_STA);

  // Set the radio to the configured ESP-NOW channel so peers match the home
  // channel.  Without this the STA defaults to channel 1 and peer sends fail
  // with "Peer channel is not equal to the home channel".
  if (config_.channel > 0) {
    esp_wifi_set_channel(config_.channel, WIFI_SECOND_CHAN_NONE);
  }

  if (esp_now_init() != ESP_OK) {
    initialized_ = false;
    return false;
  }

  esp_now_register_recv_cb(
      reinterpret_cast<esp_now_recv_cb_t>(&EspNowThermostatTransport::on_recv_static));
  esp_now_register_send_cb(
      reinterpret_cast<esp_now_send_cb_t>(&EspNowThermostatTransport::on_send_static));

  esp_now_peer_info_t peer_info;
  memset(&peer_info, 0, sizeof(peer_info));
  memcpy(peer_info.peer_addr, config_.peer_mac, sizeof(peer_info.peer_addr));
  // Use channel 0 so ESP-NOW sends on whatever the current home channel is.
  peer_info.channel = 0;
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

void EspNowThermostatTransport::set_weather_callback(
    ThermostatWeatherCallback weather_cb) {
  weather_cb_ = weather_cb;
}

void EspNowThermostatTransport::publish_command_word(uint32_t packed_word) {
  if (!initialized_) {
    return;
  }

  CommandWordPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::CommandWord);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.packed_word = packed_word;

  send_to_peer(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void EspNowThermostatTransport::publish_controller_ack(uint16_t seq) {
  if (!initialized_) {
    return;
  }

  ControllerAckPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::ControllerAck);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.seq = seq;

  send_to_peer(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void EspNowThermostatTransport::publish_indoor_temperature_c(float temp_c) {
  if (!initialized_) {
    return;
  }

  FloatValuePacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::IndoorTemperature);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.value = temp_c;

  send_to_peer(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void EspNowThermostatTransport::publish_indoor_humidity(float humidity_pct) {
  if (!initialized_) {
    return;
  }

  FloatValuePacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::IndoorHumidity);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.value = humidity_pct;

  send_to_peer(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void EspNowThermostatTransport::on_recv_static(const void *recv_info,
                                               const uint8_t *data,
                                               int len) {
  if (instance_ == nullptr) {
    return;
  }
  const uint8_t *src_mac = reinterpret_cast<const uint8_t *>(recv_info);
  instance_->on_recv(src_mac, data, len);
}

void EspNowThermostatTransport::on_send_static(const uint8_t * /*mac_addr*/, int status) {
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

void EspNowThermostatTransport::on_recv(const uint8_t *src_mac,
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
  if (!is_compatible_protocol_version(header->version)) {
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
        telemetry.seq = pkt->seq;
        telemetry.state = static_cast<FurnaceStateCode>(pkt->state);
        telemetry.lockout = pkt->lockout != 0;
        telemetry.mode_code = pkt->mode;
        telemetry.fan_code = pkt->fan;
        telemetry.setpoint_c = static_cast<float>(pkt->setpoint_decic) / 10.0f;
        telemetry.filter_runtime_seconds = pkt->filter_runtime_seconds;

        telemetry_cb_(telemetry, callback_context_);
      }
      break;

    case PacketType::WeatherData:
      if (len >= static_cast<int>(sizeof(WeatherDataPacket)) &&
          weather_cb_ != nullptr) {
        const auto *pkt = reinterpret_cast<const WeatherDataPacket *>(data);
        weather_cb_(pkt->outdoor_temp_c,
                    static_cast<WeatherIcon>(pkt->weather_icon),
                    callback_context_);
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

  send_to_peer(reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void EspNowThermostatTransport::send_to_peer(const uint8_t *data, size_t len) {
#if defined(ARDUINO)
  const uint32_t now = millis();
  if (last_send_ms_ != 0 &&
      static_cast<uint32_t>(now - last_send_ms_) < kMinSendIntervalMs) {
    return;  // Throttle: too soon since last send
  }
  last_send_ms_ = now;
  esp_now_send(config_.peer_mac, data, len);
#else
  (void)data;
  (void)len;
#endif
}

}  // namespace thermostat
