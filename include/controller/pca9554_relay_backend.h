#pragma once

#include <stdint.h>

#include "controller/relay_backend.h"
#include "thermostat_types.h"

namespace thermostat {

struct Pca9554RelayBackendConfig {
  uint8_t i2c_addr = 0x20;
  int sda_pin = 42;
  int scl_pin = 41;
  // Which PCA9554 output bit drives each HVAC relay (Waveshare: relay N = bit 1<<N).
  uint8_t heat_bit = 0;
  uint8_t cool_bit = 1;
  uint8_t fan_bit = 2;
  uint8_t spare_bit = 3;
  bool inverted = false;  // false = active-high (bit set -> relay energized)
};

// Pure, testable: map a RelayDemand to the PCA9554 output-port byte.
inline uint8_t pca9554_relay_byte(const RelayDemand &d, const Pca9554RelayBackendConfig &c) {
  uint8_t b = 0;
  if (d.heat)  b |= static_cast<uint8_t>(1u << c.heat_bit);
  if (d.cool)  b |= static_cast<uint8_t>(1u << c.cool_bit);
  if (d.fan)   b |= static_cast<uint8_t>(1u << c.fan_bit);
  if (d.spare) b |= static_cast<uint8_t>(1u << c.spare_bit);
  if (c.inverted) b = static_cast<uint8_t>(~b);
  return b;
}

class Pca9554RelayBackend : public RelayBackend {
 public:
  explicit Pca9554RelayBackend(const Pca9554RelayBackendConfig &config = Pca9554RelayBackendConfig())
      : config_(config) {}

  void begin() override;
  void write(const RelayDemand &out) override;

 private:
  void write_reg(uint8_t reg, uint8_t val);
  Pca9554RelayBackendConfig config_{};
};

}  // namespace thermostat
