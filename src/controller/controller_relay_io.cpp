#include "controller/controller_relay_io.h"

namespace thermostat {

void ControllerRelayIo::begin() {
  backend_.begin();
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
  backend_.write(out);
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
