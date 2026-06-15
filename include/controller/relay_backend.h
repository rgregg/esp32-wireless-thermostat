#pragma once

#include "thermostat_types.h"  // RelayDemand

namespace thermostat {

// Hardware seam for relay output. ControllerRelayIo owns the interlock logic and
// calls a backend to actually drive the relays. Implementations: GpioRelayBackend
// (direct GPIO), Pca9554RelayBackend (I2C expander, added with the S3 board),
// FakeRelayBackend (tests).
class RelayBackend {
 public:
  virtual ~RelayBackend() = default;

  // Initialize hardware (pin modes, expander config) and drive all relays off.
  virtual void begin() = 0;

  // Drive the relays to exactly match `out`.
  virtual void write(const RelayDemand &out) = 0;
};

}  // namespace thermostat
