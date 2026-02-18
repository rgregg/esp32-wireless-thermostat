#pragma once

#include <stdint.h>

enum class FurnaceMode : uint8_t {
  Off = 0,
  Heat = 1,
  Cool = 2,
};

enum class FanMode : uint8_t {
  Automatic = 0,
  AlwaysOn = 1,
  Circulate = 2,
};

enum class FurnaceStateCode : uint8_t {
  Idle = 0,
  HeatMode = 1,
  HeatOn = 2,
  CoolMode = 3,
  CoolOn = 4,
  FanOn = 5,
  Error = 6,
};

struct RelayDemand {
  bool heat = false;
  bool cool = false;
  bool fan = false;
  bool spare = false;
};

struct ThermostatSnapshot {
  FurnaceMode mode = FurnaceMode::Off;
  FanMode fan_mode = FanMode::Automatic;
  RelayDemand relay{};
  bool hvac_lockout = false;
  bool failsafe_active = false;
};
