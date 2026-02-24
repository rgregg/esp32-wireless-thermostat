#pragma once

#include <stdint.h>

#include "controller/controller_runtime.h"
#include "weather_icon.h"

namespace thermostat {

struct ControllerTelemetry {
  uint16_t seq = 0;
  FurnaceStateCode state = FurnaceStateCode::Error;
  float filter_runtime_hours = 0.0f;
  bool lockout = false;
  uint8_t mode_code = 0;  // off=0, heat=1, cool=2
  uint8_t fan_code = 0;   // auto=0, on=1, circulate=2
  float setpoint_c = 0.0f;
};

class IControllerTransport {
 public:
  virtual ~IControllerTransport() = default;

  virtual void publish_telemetry(const ControllerTelemetry &telemetry) = 0;
  virtual void publish_weather(float outdoor_temp_c, WeatherIcon icon) {
    (void)outdoor_temp_c;
    (void)icon;
  }
};

class ControllerApp {
 public:
  ControllerApp(IControllerTransport &transport,
                const ControllerConfig &config = ControllerConfig());

  void on_heartbeat(uint32_t now_ms);
  CommandApplyResult on_command_word(uint32_t packed_word,
                                    const uint8_t *source_mac = nullptr);
  void on_thermostat_ack(uint16_t seq);
  void set_hvac_lockout(bool locked);
  void reset_remote_command_sequence();

  // Weather state: controller stores latest weather for MQTT publishing
  void set_outdoor_weather(float temp_c, WeatherIcon icon);
  bool has_outdoor_weather() const { return has_outdoor_weather_; }
  float outdoor_temp_c() const { return outdoor_temp_c_; }
  WeatherIcon outdoor_icon() const { return outdoor_icon_; }

  void on_indoor_temperature_c(float temp_c, const uint8_t *src_mac = nullptr);
  void on_indoor_humidity(float humidity_pct, const uint8_t *src_mac = nullptr);

  void set_primary_sensor_mac(const uint8_t *mac);
  const uint8_t *primary_sensor_mac() const { return primary_sensor_mac_; }
  bool primary_sensor_auto_claimed() const { return primary_sensor_auto_claimed_; }

  // Automatic HVAC calls derived from mode + setpoint + indoor temperature.
  void tick(uint32_t now_ms);

  // Manual override entry point (useful for tests).
  void tick(uint32_t now_ms, bool heat_call, bool cool_call);

  const ControllerRuntime &runtime() const { return runtime_; }

  bool has_indoor_temperature() const { return has_indoor_temp_; }
  float indoor_temperature_c() const { return indoor_temp_c_; }
  bool has_indoor_humidity() const { return has_indoor_humidity_; }
  float indoor_humidity_pct() const { return indoor_humidity_pct_; }

 private:
  void load_persisted_state();
  void persist_indoor_state() const;
  void maybe_persist_filter_runtime();
  bool telemetry_payload_changed(const ControllerTelemetry &next) const;
  static bool is_newer_u16(uint16_t previous, uint16_t incoming);
  static uint8_t mode_to_code(FurnaceMode mode);
  static uint8_t fan_to_code(FanMode mode);

  void compute_hvac_calls(bool *heat_call, bool *cool_call) const;
  void publish();

  IControllerTransport &transport_;
  ControllerConfig config_{};
  ControllerRuntime runtime_;

  bool has_indoor_temp_ = true;
  bool has_indoor_humidity_ = true;
  float indoor_temp_c_ = 20.0f;
  float indoor_humidity_pct_ = 50.0f;

  uint16_t telemetry_seq_ = 0;
  uint16_t last_acked_seq_ = 0;
  bool has_last_published_ = false;
  ControllerTelemetry last_published_{};

  uint8_t primary_sensor_mac_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool primary_sensor_auto_claimed_ = false;

  bool has_outdoor_weather_ = false;
  float outdoor_temp_c_ = 0.0f;
  WeatherIcon outdoor_icon_ = WeatherIcon::Unknown;

  uint32_t persisted_filter_runtime_s_ = 0;
};

}  // namespace thermostat
