#pragma once

#include <stdint.h>

#include "controller_runtime.h"

namespace thermostat {

struct ControllerTelemetry {
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
};

class ControllerApp {
 public:
  ControllerApp(IControllerTransport &transport,
                const ControllerConfig &config = ControllerConfig());

  void on_heartbeat(uint32_t now_ms);
  CommandApplyResult on_command_word(uint32_t packed_word);
  void set_hvac_lockout(bool locked);

  void on_indoor_temperature_c(float temp_c);
  void on_indoor_humidity(float humidity_pct);

  // Automatic HVAC calls derived from mode + setpoint + indoor temperature.
  void tick(uint32_t now_ms);

  // Manual override entry point (useful for tests).
  void tick(uint32_t now_ms, bool heat_call, bool cool_call);

  const ControllerRuntime &runtime() const { return runtime_; }

  bool has_indoor_temperature() const { return has_indoor_temp_; }
  float indoor_temperature_c() const { return indoor_temp_c_; }

 private:
  static uint8_t mode_to_code(FurnaceMode mode);
  static uint8_t fan_to_code(FanMode mode);

  void compute_hvac_calls(bool *heat_call, bool *cool_call) const;
  void publish();

  IControllerTransport &transport_;
  ControllerRuntime runtime_;

  bool has_indoor_temp_ = false;
  bool has_indoor_humidity_ = false;
  float indoor_temp_c_ = 0.0f;
  float indoor_humidity_pct_ = 0.0f;
};

}  // namespace thermostat
