#pragma once

#include <stdint.h>

#include "espnow_cmd_word.h"
#include "thermostat_types.h"

namespace thermostat {

struct ControllerConfig {
  uint32_t failsafe_timeout_ms = 300000;
  uint16_t fan_circulate_period_min = 60;
  uint16_t fan_circulate_duration_min = 10;
  uint32_t min_cooling_off_time_ms = 300000;
  uint32_t min_cooling_run_time_ms = 300000;
  uint32_t min_heating_off_time_ms = 180000;
  uint32_t min_heating_run_time_ms = 180000;
  uint32_t min_idle_time_ms = 30000;
  float heat_deadband_c = 0.5f;
  float heat_overrun_c = 0.5f;
  float cool_deadband_c = 0.5f;
  float cool_overrun_c = 0.5f;
  float indoor_temp_fallback_c = 20.0f;
  float indoor_humidity_fallback_pct = 50.0f;
};

struct ControllerTickInput {
  uint32_t now_ms = 0;
  bool heat_call = false;
  bool cool_call = false;
};

struct CommandApplyResult {
  bool accepted = false;
  bool stale_or_duplicate = false;
  bool sync_requested = false;
  bool filter_reset_requested = false;
};

class ControllerRuntime {
 public:
  explicit ControllerRuntime(const ControllerConfig &config = ControllerConfig());

  void note_heartbeat(uint32_t now_ms);
  void set_hvac_lockout(bool locked_out);
  void reset_remote_command_sequence();

  CommandApplyResult apply_remote_command(const CommandWord &cmd,
                                          const uint8_t *source_mac = nullptr);

  void tick(const ControllerTickInput &in);

  FurnaceMode mode() const { return mode_; }
  FanMode fan_mode() const { return fan_mode_; }
  float target_temperature_c() const { return target_temperature_c_; }

  bool failsafe_active() const { return failsafe_active_; }
  bool hvac_lockout() const { return hvac_lockout_; }

  bool heat_demand() const { return relay_.heat; }
  bool cool_demand() const { return relay_.cool; }
  bool fan_demand() const { return relay_.fan; }
  uint32_t heartbeat_last_seen_ms() const { return heartbeat_last_seen_ms_; }

  uint32_t filter_runtime_seconds() const { return filter_runtime_seconds_; }
  float filter_runtime_hours() const {
    return static_cast<float>(filter_runtime_seconds_) / 3600.0f;
  }

  FurnaceStateCode furnace_state() const;
  ThermostatSnapshot snapshot() const;

 private:
  enum class HvacState : uint8_t {
    Idle = 0,
    Heating = 1,
    Cooling = 2,
  };

  void enforce_safety_interlocks(uint32_t now_ms);
  void update_failsafe(uint32_t now_ms);
  void apply_hvac_calls(uint32_t now_ms, bool heat_call, bool cool_call);
  bool elapsed_at_least(uint32_t now_ms, uint32_t start_ms, uint32_t duration_ms) const;
  void enter_idle(uint32_t now_ms);
  void enter_heating(uint32_t now_ms);
  void enter_cooling(uint32_t now_ms);
  void run_minute_tasks(uint32_t now_ms);

  ControllerConfig config_;

  FurnaceMode mode_ = FurnaceMode::Off;
  FanMode fan_mode_ = FanMode::Automatic;
  float target_temperature_c_ = 20.0f;

  bool hvac_lockout_ = false;
  bool failsafe_active_ = false;
  bool spare_relay_4_ = false;

  RelayDemand relay_{};

  uint32_t heartbeat_last_seen_ms_ = 0;
  uint32_t filter_runtime_seconds_ = 0;
  uint32_t hvac_state_since_ms_ = 0;
  uint32_t last_heating_off_ms_ = 0;
  uint32_t last_cooling_off_ms_ = 0;
  bool has_heating_off_timestamp_ = false;
  bool has_cooling_off_timestamp_ = false;
  HvacState hvac_state_ = HvacState::Idle;

  int fan_circulate_elapsed_min_ = 0;
  uint32_t last_minute_tick_ms_ = 0;

  static constexpr int kMaxCommandSources = 10;
  struct CommandSource {
    uint8_t mac[6] = {};
    uint16_t last_seq = 0;
    bool active = false;
  };
  CommandSource command_sources_[kMaxCommandSources] = {};
  uint16_t last_seq_default_ = 0;  // used when no source MAC provided
};

}  // namespace thermostat
