#include "thermostat_state.h"

namespace thermostat {

FurnaceStateCode compute_furnace_state(const ThermostatSnapshot &snapshot) {
  if (snapshot.failsafe_active || snapshot.hvac_lockout) {
    return FurnaceStateCode::Error;
  }
  if (snapshot.relay.heat) {
    return FurnaceStateCode::HeatOn;
  }
  if (snapshot.relay.cool) {
    return FurnaceStateCode::CoolOn;
  }
  if (snapshot.relay.fan) {
    return FurnaceStateCode::FanOn;
  }

  switch (snapshot.mode) {
    case FurnaceMode::Heat:
      return FurnaceStateCode::HeatMode;
    case FurnaceMode::Cool:
      return FurnaceStateCode::CoolMode;
    case FurnaceMode::Off:
      return FurnaceStateCode::Idle;
    default:
      return FurnaceStateCode::Error;
  }
}

bool is_failsafe_timed_out(uint32_t now_ms, uint32_t heartbeat_last_seen_ms,
                           uint32_t timeout_ms) {
  if (heartbeat_last_seen_ms == 0) {
    return now_ms > timeout_ms;
  }
  return (now_ms - heartbeat_last_seen_ms) > timeout_ms;
}

}  // namespace thermostat
