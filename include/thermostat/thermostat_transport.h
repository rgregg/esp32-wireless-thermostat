#pragma once

#include <stdint.h>

namespace thermostat {

// Delivery outcome of the latest command frame, from the ESP-NOW send callback.
enum class CommandDelivery : uint8_t { None = 0, Pending = 1, Delivered = 2, Failed = 3 };

class IThermostatTransport {
 public:
  virtual ~IThermostatTransport() = default;

  // Both return false only when the frame was not attempted (send throttle); the app
  // retries.
  virtual bool publish_command_word(uint32_t packed_word) = 0;
  virtual bool publish_controller_ack(uint16_t seq) = 0;
  // Delivered or Failed is reported once for the latest command frame, then None; Pending
  // while its send callback is outstanding. A newer command frame replaces the old one.
  virtual CommandDelivery take_command_delivery() { return CommandDelivery::None; }
  virtual bool reports_command_delivery() const { return false; }
  virtual void publish_indoor_temperature_c(float temp_c) = 0;
  virtual void publish_indoor_humidity(float humidity_pct) = 0;
};

}  // namespace thermostat
