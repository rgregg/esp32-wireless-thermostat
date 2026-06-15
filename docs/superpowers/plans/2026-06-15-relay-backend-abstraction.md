# Relay-Backend Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `ControllerRelayIo` so its hardware writes go through an injected `RelayBackend` interface, with a `GpioRelayBackend` preserving today's exact behavior — the seam the ESP32-S3 board's PCA9554 backend will later plug into.

**Architecture:** Keep all interlock/force-off/pending logic in `ControllerRelayIo` (unchanged). Extract the two hardware-touching operations (`begin`, `write_outputs`) into a `RelayBackend` abstract interface. Provide `GpioRelayBackend` (direct GPIO, current behavior) for firmware and a `FakeRelayBackend` (records writes) for native tests. No behavior change on the existing board.

**Tech Stack:** C++ (Arduino-ESP32 / native), PlatformIO `native-tests` env, project `TEST_CASE`/`ASSERT_*` harness.

---

## File Structure

- **Create** `include/controller/relay_backend.h` — `RelayBackend` abstract interface (`begin()`, `write()`).
- **Create** `include/controller/gpio_relay_backend.h` — `GpioRelayBackend` + `GpioRelayBackendConfig` (pins/inverted).
- **Create** `src/controller/gpio_relay_backend.cpp` — GPIO impl (Arduino-guarded), plus a native-testable level helper.
- **Modify** `include/controller/controller_relay_io.h` — constructor takes `RelayBackend&`; `ControllerRelayIoConfig` keeps only interlock waits.
- **Modify** `src/controller/controller_relay_io.cpp` — `begin()`/`write_outputs()` delegate to the backend.
- **Modify** `src/esp32_controller_main.cpp:45` — construct a `GpioRelayBackend` and inject it into `g_relay_io`.
- **Modify** `src/tests/test_controller_relay_io.cpp` — construct via a `FakeRelayBackend`; add delegation test.
- **Create** `src/tests/test_gpio_relay_backend.cpp` — unit-test the GPIO level helper.

**Invariants:** native tests stay green throughout; the controller firmware keeps producing identical relay behavior (same pins 32/33/25/26, non-inverted).

---

## Task 1: Define the `RelayBackend` interface

**Files:**
- Create: `include/controller/relay_backend.h`

- [ ] **Step 1: Create the interface header**

```cpp
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
```

- [ ] **Step 2: Verify it compiles in the native test build**

Run: `pio run -e native-tests`
Expected: SUCCESS (header is included transitively later; this confirms no syntax error). If nothing includes it yet, this just rebuilds clean.

- [ ] **Step 3: Commit**

```bash
git add include/controller/relay_backend.h
git commit -m "feat(controller): add RelayBackend interface (relay HW seam)"
```

---

## Task 2: Inject the backend into `ControllerRelayIo` (TDD)

**Files:**
- Modify: `include/controller/controller_relay_io.h`
- Modify: `src/controller/controller_relay_io.cpp`
- Modify: `src/tests/test_controller_relay_io.cpp`

- [ ] **Step 1: Add a `FakeRelayBackend` and a failing delegation test**

At the top of `src/tests/test_controller_relay_io.cpp`, after the existing includes, add the fake and a new test. The fake records the last write and a count:

```cpp
#include "controller/relay_backend.h"

namespace {
class FakeRelayBackend : public thermostat::RelayBackend {
 public:
  void begin() override { begin_calls++; }
  void write(const RelayDemand &out) override { last = out; writes++; }
  RelayDemand last{};
  int writes = 0;
  int begin_calls = 0;
};
}  // namespace

TEST_CASE(controller_relay_io_delegates_writes_to_backend) {
  FakeRelayBackend backend;
  thermostat::ControllerRelayIo io(backend);
  io.begin();
  ASSERT_EQ(backend.begin_calls, 1);

  RelayDemand heat;
  heat.heat = true;
  io.apply(0, heat, false);
  ASSERT_TRUE(backend.last.heat);
  ASSERT_TRUE(!backend.last.cool);

  // force_off must drive the backend to all-off.
  io.apply(1, heat, true);
  ASSERT_TRUE(!backend.last.heat);
  ASSERT_TRUE(!backend.last.cool);
  ASSERT_TRUE(!backend.last.fan);
  ASSERT_TRUE(!backend.last.spare);
}
```

- [ ] **Step 2: Run the test — verify it fails to compile**

Run: `pio run -e native-tests`
Expected: FAIL — `ControllerRelayIo` has no constructor taking a `RelayBackend&`.

- [ ] **Step 3: Update the header to take an injected backend**

In `include/controller/controller_relay_io.h`: add `#include "controller/relay_backend.h"`, drop pin/inverted fields from the config, change the constructor, and store a backend reference. Replace the struct + constructor + the private `config_` member:

```cpp
#pragma once

#include <stdint.h>

#include "controller/relay_backend.h"
#include "thermostat_types.h"

namespace thermostat {

struct ControllerRelayIoConfig {
  uint32_t heat_interlock_wait_ms = 500;
  uint32_t default_interlock_wait_ms = 1000;
};

class ControllerRelayIo {
 public:
  explicit ControllerRelayIo(RelayBackend &backend,
                             const ControllerRelayIoConfig &config = ControllerRelayIoConfig())
      : backend_(backend), config_(config) {}
```

…and change the private data members (replace the existing `config_` line):

```cpp
  RelayBackend &backend_;
  ControllerRelayIoConfig config_{};
```

(Keep the rest of the class — `apply`, `latched_output`, `has_pending`, `pending_*`, the `RelaySelect` enum, and the private method declarations — exactly as-is.)

- [ ] **Step 4: Update the .cpp to delegate to the backend**

In `src/controller/controller_relay_io.cpp`: remove the `#if defined(ARDUINO)` Arduino include and the `relay_on_level`/`relay_off_level` anonymous-namespace helpers (they move to the GPIO backend in Task 3). Replace `begin()` and `write_outputs()` with:

```cpp
void ControllerRelayIo::begin() {
  backend_.begin();
  write_outputs(RelayDemand{});
  initialized_ = true;
  active_ = RelaySelect::None;
  pending_ = RelaySelect::None;
  pending_since_ms_ = 0;
}
```

```cpp
void ControllerRelayIo::write_outputs(const RelayDemand &out) {
  output_ = out;
  backend_.write(out);
}
```

(Leave `pick_single`, `wait_ms_for`, `to_demand`, and `apply` unchanged.)

- [ ] **Step 5: Update the existing tests to construct with a backend**

Every existing `ControllerRelayIo io(cfg);` / `ControllerRelayIo io;` in `src/tests/test_controller_relay_io.cpp` must now pass a backend. For each test case, declare a `FakeRelayBackend backend;` first and change construction to `thermostat::ControllerRelayIo io(backend);` or `thermostat::ControllerRelayIo io(backend, cfg);`. The assertions on `io.latched_output()` stay unchanged.

- [ ] **Step 6: Run the tests — verify all pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, total test count = previous + 1.

- [ ] **Step 7: Commit**

```bash
git add include/controller/controller_relay_io.h src/controller/controller_relay_io.cpp src/tests/test_controller_relay_io.cpp
git commit -m "refactor(controller): ControllerRelayIo writes via injected RelayBackend"
```

---

## Task 3: Add `GpioRelayBackend` with a testable level helper (TDD)

**Files:**
- Create: `include/controller/gpio_relay_backend.h`
- Create: `src/controller/gpio_relay_backend.cpp`
- Create: `src/tests/test_gpio_relay_backend.cpp`

- [ ] **Step 1: Write the failing level-helper test**

Create `src/tests/test_gpio_relay_backend.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/gpio_relay_backend.h"
#include "test_harness.h"

TEST_CASE(gpio_relay_level_non_inverted) {
  // Non-inverted: demand on -> HIGH (true), off -> LOW (false).
  ASSERT_TRUE(thermostat::gpio_relay_level(true, false) == true);
  ASSERT_TRUE(thermostat::gpio_relay_level(false, false) == false);
}

TEST_CASE(gpio_relay_level_inverted) {
  // Inverted (active-low): demand on -> LOW (false), off -> HIGH (true).
  ASSERT_TRUE(thermostat::gpio_relay_level(true, true) == false);
  ASSERT_TRUE(thermostat::gpio_relay_level(false, true) == true);
}
#endif  // THERMOSTAT_RUN_TESTS
```

- [ ] **Step 2: Run — verify it fails to compile**

Run: `pio run -e native-tests`
Expected: FAIL — `gpio_relay_backend.h` does not exist.

- [ ] **Step 3: Create the GPIO backend header**

Create `include/controller/gpio_relay_backend.h`:

```cpp
#pragma once

#include "controller/relay_backend.h"
#include "thermostat_types.h"

namespace thermostat {

struct GpioRelayBackendConfig {
  int heat_pin = 32;
  int cool_pin = 33;
  int fan_pin = 25;
  int spare_pin = 26;
  bool inverted = false;
};

// Pure, testable: the digital level to drive for a given relay demand + polarity.
inline bool gpio_relay_level(bool demand_on, bool inverted) {
  return inverted ? !demand_on : demand_on;
}

class GpioRelayBackend : public RelayBackend {
 public:
  explicit GpioRelayBackend(const GpioRelayBackendConfig &config = GpioRelayBackendConfig())
      : config_(config) {}

  void begin() override;
  void write(const RelayDemand &out) override;

 private:
  GpioRelayBackendConfig config_{};
};

}  // namespace thermostat
```

- [ ] **Step 4: Create the GPIO backend implementation**

Create `src/controller/gpio_relay_backend.cpp`:

```cpp
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
```

- [ ] **Step 5: Run — verify the tests pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, count increased by 2.

- [ ] **Step 6: Commit**

```bash
git add include/controller/gpio_relay_backend.h src/controller/gpio_relay_backend.cpp src/tests/test_gpio_relay_backend.cpp
git commit -m "feat(controller): add GpioRelayBackend (direct GPIO relay impl)"
```

---

## Task 4: Wire `GpioRelayBackend` into firmware `main`

**Files:**
- Modify: `src/esp32_controller_main.cpp:45`

- [ ] **Step 1: Construct the backend and inject it**

Replace the single global definition at `src/esp32_controller_main.cpp:45`:

```cpp
thermostat::ControllerRelayIo g_relay_io;
```

with (backend declared first so the reference binds at static-init):

```cpp
#include "controller/gpio_relay_backend.h"
// ...
thermostat::GpioRelayBackend g_relay_backend;          // defaults: pins 32/33/25/26, non-inverted
thermostat::ControllerRelayIo g_relay_io(g_relay_backend);
```

(Add the `#include` near the other `controller/...` includes at the top of the file; place the two definitions together where line 45 was. The defaults reproduce the previous pin mapping exactly, so relay behavior is unchanged. `g_relay_io.begin()` at :2406 and `g_relay_io.apply(...)` at :2459 stay as-is.)

- [ ] **Step 2: Build the controller firmware**

Run: `pio run -e esp32-furnace-controller`
Expected: SUCCESS, `firmware.bin` produced.

- [ ] **Step 3: Build the thermostat firmware (it shares headers) as a regression check**

Run: `pio run -e esp32-furnace-thermostat`
Expected: SUCCESS.

- [ ] **Step 4: Run the full native test suite**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, no regressions.

- [ ] **Step 5: Commit**

```bash
git add src/esp32_controller_main.cpp
git commit -m "refactor(controller): wire GpioRelayBackend into controller main"
```

---

## Self-Review

**Spec coverage:** Implements the spec's §1 (relay HW abstraction) and the build-config "relay backend" row — the `GpioRelayBackend` for the old board and the `RelayBackend` seam the future `Pca9554RelayBackend` (Plan 4) plugs into. The platform-agnostic bit-packing header is deferred to Plan 4 (only the PCA9554 backend needs it — YAGNI here). No behavior change, satisfying the "old board keeps building / working fallback" invariant.

**Placeholder scan:** None — every step has exact paths, full code, exact commands, expected output.

**Type consistency:** `RelayBackend::begin()`/`write(const RelayDemand&)` are used identically in `FakeRelayBackend`, `GpioRelayBackend`, and `ControllerRelayIo::{begin,write_outputs}`. `GpioRelayBackendConfig` field names (`heat_pin`/`cool_pin`/`fan_pin`/`spare_pin`/`inverted`) match the old `ControllerRelayIoConfig` defaults (32/33/25/26, false). `gpio_relay_level(demand_on, inverted)` signature matches its tests and call sites. Constructor `ControllerRelayIo(RelayBackend&, config)` matches all updated call sites (tests + main).

**Risk note for executor:** this touches the live furnace controller's relay path. It is a pure refactor (identical pins/levels), but validate by diffing relay behavior before/after on the bench, and follow the flashing runbook (HVAC Off + 60 s settle) before any controller flash.
