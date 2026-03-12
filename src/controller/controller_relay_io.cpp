#include "controller/controller_relay_io.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>
#endif

namespace thermostat {

namespace {

bool relay_on_level(bool inverted) { return !inverted; }
bool relay_off_level(bool inverted) { return inverted; }

}  // namespace

void ControllerRelayIo::begin() {
#if defined(ARDUINO)
  if (config_.use_i2c) {
    // Configure XL9535 port 0 as all-outputs (register 0x06 = 0x00)
    config_.i2c_bus->beginTransmission(config_.i2c_address);
    config_.i2c_bus->write(0x06);  // config register, port 0
    config_.i2c_bus->write(0x00);  // all outputs
    uint8_t result = config_.i2c_bus->endTransmission();
    Serial.printf("[RelayIO] XL9535 init at 0x%02X: %s (code %d)\n",
                  config_.i2c_address,
                  result == 0 ? "OK" : "FAILED",
                  result);
  } else {
    pinMode(config_.heat_pin, OUTPUT);
    pinMode(config_.cool_pin, OUTPUT);
    pinMode(config_.fan_pin, OUTPUT);
    pinMode(config_.spare_pin, OUTPUT);
  }
#endif
  write_outputs(RelayDemand{});
  initialized_ = true;
  active_ = RelaySelect::None;
  pending_ = RelaySelect::None;
  pending_since_ms_ = 0;
}

ControllerRelayIo::RelaySelect ControllerRelayIo::pick_single(const RelayDemand &demand) {
  if (demand.heat) return RelaySelect::Heat;
  if (demand.cool) return RelaySelect::Cool;
  if (demand.fan) return RelaySelect::Fan;
  if (demand.spare) return RelaySelect::Spare;
  return RelaySelect::None;
}

uint32_t ControllerRelayIo::wait_ms_for(RelaySelect relay) const {
  if (relay == RelaySelect::Heat) {
    return config_.heat_interlock_wait_ms;
  }
  if (relay == RelaySelect::None) {
    return 0;
  }
  return config_.default_interlock_wait_ms;
}

RelayDemand ControllerRelayIo::to_demand(RelaySelect relay) const {
  RelayDemand out;
  switch (relay) {
    case RelaySelect::Heat:
      out.heat = true;
      break;
    case RelaySelect::Cool:
      out.cool = true;
      break;
    case RelaySelect::Fan:
      out.fan = true;
      break;
    case RelaySelect::Spare:
      out.spare = true;
      break;
    case RelaySelect::None:
    default:
      break;
  }
  return out;
}

void ControllerRelayIo::write_outputs(const RelayDemand &out) {
  output_ = out;
#if defined(ARDUINO)
  if (config_.use_i2c) {
    const uint8_t relay_bits = (out.heat ? 1 : 0) |
                               (out.cool ? 2 : 0) |
                               (out.fan  ? 4 : 0) |
                               (out.spare? 8 : 0);
    const uint8_t relay_mask = static_cast<uint8_t>(0x0F << config_.i2c_relay_offset);
    const uint8_t shifted    = static_cast<uint8_t>(relay_bits << config_.i2c_relay_offset);
    const uint8_t bitmask    = config_.inverted
                               ? static_cast<uint8_t>(relay_mask & ~shifted)
                               : shifted;
    config_.i2c_bus->beginTransmission(config_.i2c_address);
    config_.i2c_bus->write(0x02);   // output port 0 register
    config_.i2c_bus->write(bitmask);
    uint8_t result = config_.i2c_bus->endTransmission();
    if (result != 0) {
      Serial.printf("[RelayIO] I2C write failed (code %d)\n", result);
    }
  } else {
    const bool on = relay_on_level(config_.inverted);
    const bool off = relay_off_level(config_.inverted);
    digitalWrite(config_.heat_pin, out.heat ? on : off);
    digitalWrite(config_.cool_pin, out.cool ? on : off);
    digitalWrite(config_.fan_pin,  out.fan  ? on : off);
    digitalWrite(config_.spare_pin,out.spare? on : off);
  }
#endif
}

void ControllerRelayIo::apply(uint32_t now_ms, const RelayDemand &desired, bool force_off) {
  if (!initialized_) {
    begin();
  }

  const RelaySelect requested = force_off ? RelaySelect::None : pick_single(desired);

  if (requested == RelaySelect::None) {
    pending_ = RelaySelect::None;
    pending_since_ms_ = 0;
    active_ = RelaySelect::None;
    write_outputs(RelayDemand{});
    return;
  }

  if (requested == active_) {
    pending_ = RelaySelect::None;
    pending_since_ms_ = 0;
    write_outputs(to_demand(active_));
    return;
  }

  // If everything is already off, allow immediate activation.
  if (active_ == RelaySelect::None && pending_ == RelaySelect::None) {
    active_ = requested;
    write_outputs(to_demand(active_));
    return;
  }

  // Any transition to a different relay first forces all relays off, then waits.
  if (pending_ != requested) {
    pending_ = requested;
    pending_since_ms_ = now_ms;
    active_ = RelaySelect::None;
    write_outputs(RelayDemand{});
    return;
  }

  const uint32_t wait_ms = wait_ms_for(pending_);
  if (static_cast<uint32_t>(now_ms - pending_since_ms_) < wait_ms) {
    write_outputs(RelayDemand{});
    return;
  }

  active_ = pending_;
  pending_ = RelaySelect::None;
  pending_since_ms_ = 0;
  write_outputs(to_demand(active_));
}

}  // namespace thermostat
