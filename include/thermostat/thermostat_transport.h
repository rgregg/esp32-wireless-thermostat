#pragma once

#include <stdint.h>

namespace thermostat {

class IThermostatTransport {
 public:
  virtual ~IThermostatTransport() = default;

  // Both return false only when the frame was not attempted (send throttle); the app
  // retries.
  virtual bool publish_command_word(uint32_t packed_word) = 0;
  virtual bool publish_controller_ack(uint16_t seq) = 0;
  virtual void publish_indoor_temperature_c(float temp_c) = 0;
  virtual void publish_indoor_humidity(float humidity_pct) = 0;
};

}  // namespace thermostat
