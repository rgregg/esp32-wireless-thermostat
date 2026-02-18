#pragma once

#include <stdint.h>

#include "thermostat/thermostat_transport.h"
#include "thermostat_types.h"
#include "transport/espnow_packets.h"

namespace thermostat {

struct ThermostatControllerTelemetry {
  uint16_t seq = 0;
  FurnaceStateCode state = FurnaceStateCode::Error;
  bool lockout = false;
  uint8_t mode_code = 0;
  uint8_t fan_code = 0;
  float setpoint_c = 0.0f;
  uint32_t filter_runtime_seconds = 0;
};

struct EspNowThermostatConfig {
  uint8_t peer_mac[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 6;
  bool encrypted = false;
  uint8_t lmk[16] = {0};
  uint32_t heartbeat_interval_ms = 10000;
};

using ThermostatHeartbeatCallback = void (*)(uint32_t now_ms, void *ctx);
using ThermostatTelemetryCallback =
    void (*)(const ThermostatControllerTelemetry &telemetry, void *ctx);

class EspNowThermostatTransport final : public IThermostatTransport {
 public:
  bool begin(const EspNowThermostatConfig &config);
  void loop(uint32_t now_ms);

  void set_callbacks(ThermostatHeartbeatCallback heartbeat_cb,
                     ThermostatTelemetryCallback telemetry_cb,
                     void *callback_context);

  void publish_command_word(uint32_t packed_word) override;
  void publish_controller_ack(uint16_t seq) override;
  void publish_indoor_temperature_c(float temp_c) override;
  void publish_indoor_humidity(float humidity_pct) override;
  uint32_t send_ok_count() const { return send_ok_count_; }
  uint32_t send_fail_count() const { return send_fail_count_; }

 private:
  static void on_recv_static(const void *recv_info, const uint8_t *data, int len);
  static void on_send_static(const uint8_t *mac_addr, int status);
  void on_recv(const uint8_t *src_mac, const uint8_t *data, int len);
  void send_heartbeat(uint32_t now_ms);

  EspNowThermostatConfig config_{};
  bool initialized_ = false;
  uint32_t last_heartbeat_sent_ms_ = 0;
  bool heartbeat_toggle_ = false;

  ThermostatHeartbeatCallback heartbeat_cb_ = nullptr;
  ThermostatTelemetryCallback telemetry_cb_ = nullptr;
  void *callback_context_ = nullptr;

  static EspNowThermostatTransport *instance_;
  uint32_t send_ok_count_ = 0;
  uint32_t send_fail_count_ = 0;
};

}  // namespace thermostat
