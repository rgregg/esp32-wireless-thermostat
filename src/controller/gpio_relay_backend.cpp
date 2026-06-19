#include "controller/gpio_relay_backend.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace thermostat {

void GpioRelayBackend::begin() {
#if defined(ARDUINO)
  pinMode(config_.heat_pin, OUTPUT);
  pinMode(config_.cool_pin, OUTPUT);
  pinMode(config_.fan_pin, OUTPUT);
  pinMode(config_.spare_pin, OUTPUT);
#endif
  write(RelayDemand{});
}

void GpioRelayBackend::write(const RelayDemand &out) {
#if defined(ARDUINO)
  digitalWrite(config_.heat_pin, gpio_relay_level(out.heat, config_.inverted));
  digitalWrite(config_.cool_pin, gpio_relay_level(out.cool, config_.inverted));
  digitalWrite(config_.fan_pin, gpio_relay_level(out.fan, config_.inverted));
  digitalWrite(config_.spare_pin, gpio_relay_level(out.spare, config_.inverted));
#else
  (void)out;
#endif
}

}  // namespace thermostat
