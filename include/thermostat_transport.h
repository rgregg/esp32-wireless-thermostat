#pragma once

#include <stdint.h>

namespace thermostat {

class IThermostatTransport {
 public:
  virtual ~IThermostatTransport() = default;

  virtual void publish_command_word(uint32_t packed_word) = 0;
  virtual void publish_indoor_temperature_c(float temp_c) = 0;
  virtual void publish_indoor_humidity(float humidity_pct) = 0;
};

}  // namespace thermostat
