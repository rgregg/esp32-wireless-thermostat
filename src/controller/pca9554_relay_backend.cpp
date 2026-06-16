#include "controller/pca9554_relay_backend.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>

namespace thermostat {

namespace {
constexpr uint8_t kPca9554RegOutput = 0x01;
constexpr uint8_t kPca9554RegConfig = 0x03;  // 1=input, 0=output
}  // namespace

void Pca9554RelayBackend::write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(config_.i2c_addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void Pca9554RelayBackend::begin() {
  Wire.begin(config_.sda_pin, config_.scl_pin);
  Wire.setClock(100000);
  // Safe-init: drive the output register to the all-OFF value BEFORE switching the
  // pins to outputs, so the input->output transition cannot momentarily energize a
  // relay (the PCA9554 output-register POR default is 0xFF). "All off" is 0x00 for
  // active-high, 0xFF for active-low.
  write_reg(kPca9554RegOutput, pca9554_relay_byte(RelayDemand{}, config_));
  write_reg(kPca9554RegConfig, 0x00);  // all pins outputs
}

void Pca9554RelayBackend::write(const RelayDemand &out) {
  write_reg(kPca9554RegOutput, pca9554_relay_byte(out, config_));
}

}  // namespace thermostat

#else  // !ARDUINO

namespace thermostat {
void Pca9554RelayBackend::write_reg(uint8_t, uint8_t) {}
void Pca9554RelayBackend::begin() {}
void Pca9554RelayBackend::write(const RelayDemand &) {}
}  // namespace thermostat

#endif  // ARDUINO
