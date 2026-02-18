#include "thermostat_ui_state.h"

namespace thermostat {

std::string furnace_state_text(FurnaceStateCode state,
                               bool connected,
                               bool lockout,
                               bool failsafe_active) {
  if (failsafe_active) {
    return "Failsafe";
  }
  if (lockout) {
    return "Locked Out";
  }
  if (!connected) {
    return "Not Connected";
  }

  switch (state) {
    case FurnaceStateCode::Idle:
      return "Idle";
    case FurnaceStateCode::HeatMode:
      return "Heat mode";
    case FurnaceStateCode::HeatOn:
      return "Heat on";
    case FurnaceStateCode::CoolMode:
      return "Cool mode";
    case FurnaceStateCode::CoolOn:
      return "Cool on";
    case FurnaceStateCode::FanOn:
      return "Fan on";
    case FurnaceStateCode::Error:
    default:
      return "Error";
  }
}

std::string furnace_mode_text(FurnaceMode mode) {
  switch (mode) {
    case FurnaceMode::Heat:
      return "Heat";
    case FurnaceMode::Cool:
      return "Cool";
    case FurnaceMode::Off:
    default:
      return "Off";
  }
}

std::string fan_mode_text(FanMode mode) {
  switch (mode) {
    case FanMode::AlwaysOn:
      return "Always On";
    case FanMode::Circulate:
      return "Circulate";
    case FanMode::Automatic:
    default:
      return "Automatic";
  }
}

}  // namespace thermostat
