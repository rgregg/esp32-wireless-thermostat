#pragma once

#include <stdint.h>

#include "espnow_cmd_word.h"
#include "thermostat/thermostat_transport.h"
#include "thermostat_types.h"
#include "thermostat/transport/espnow_thermostat_transport.h"

namespace thermostat {

struct ThermostatAppConfig {
  uint32_t local_interaction_debounce_ms = 5000;
};

class ThermostatApp {
 public:
  ThermostatApp(IThermostatTransport &transport,
                const ThermostatAppConfig &config = ThermostatAppConfig());

  void on_controller_heartbeat(uint32_t now_ms);
  void on_controller_telemetry(uint32_t now_ms,
                               const ThermostatControllerTelemetry &telemetry);

  void set_local_mode(FurnaceMode mode, uint32_t now_ms);
  void set_local_fan_mode(FanMode mode, uint32_t now_ms);
  void set_local_setpoint_c(float setpoint_c, uint32_t now_ms);

  void request_sync(uint32_t now_ms);
  void request_filter_reset(uint32_t now_ms);

  void publish_indoor_temperature_c(float temp_c);
  void publish_indoor_humidity(float humidity_pct);

  bool controller_connected(uint32_t now_ms, uint32_t timeout_ms) const;
  bool has_last_packed_command() const { return has_last_packed_command_; }
  uint32_t last_packed_command() const { return last_packed_command_; }
  uint16_t last_command_seq() const { return last_command_seq_; }

  FurnaceMode local_mode() const { return local_mode_; }
  FanMode local_fan_mode() const { return local_fan_mode_; }
  float local_setpoint_c() const { return local_setpoint_c_; }
  bool has_controller_telemetry() const { return has_controller_telemetry_; }
  FurnaceStateCode controller_state() const { return controller_state_; }
  bool controller_lockout() const { return controller_lockout_; }
  float controller_setpoint_c() const { return controller_setpoint_c_; }
  uint32_t controller_filter_runtime_seconds() const {
    return controller_filter_runtime_seconds_;
  }

 private:
  static FurnaceMode mode_from_code(uint8_t mode_code);
  static FanMode fan_from_code(uint8_t fan_code);
  static bool is_newer_u16(uint16_t previous, uint16_t incoming);

  void mark_local_interaction(uint32_t now_ms);
  void send_command(bool do_sync, bool do_filter_reset);
  void ack_controller_seq(uint16_t seq);

  IThermostatTransport &transport_;
  ThermostatAppConfig config_;

  uint16_t seq_local_ = 0;
  bool has_last_packed_command_ = false;
  uint32_t last_packed_command_ = 0;
  uint16_t last_command_seq_ = 0;
  FurnaceMode local_mode_ = FurnaceMode::Off;
  FanMode local_fan_mode_ = FanMode::Automatic;
  float local_setpoint_c_ = 20.0f;

  bool has_controller_telemetry_ = false;
  FurnaceStateCode controller_state_ = FurnaceStateCode::Error;
  bool controller_lockout_ = false;
  float controller_setpoint_c_ = 20.0f;
  uint32_t controller_filter_runtime_seconds_ = 0;

  uint32_t last_local_interaction_ms_ = 0;
  uint32_t last_controller_heartbeat_ms_ = 0;
  bool has_controller_seq_ = false;
  uint16_t last_controller_seq_ = 0;
};

}  // namespace thermostat
