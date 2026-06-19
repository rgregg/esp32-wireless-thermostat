# Resilience Increment 1: Network Recovery Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A platform-agnostic, fully unit-tested `NetworkRecoveryPolicy` that decides how to recover a network subsystem — **reconnect (with capped exponential backoff) → restart the subsystem → reboot only as a last resort** — so the firmware can stop using `esp_restart()` as its primary recovery.

**Architecture:** Pure decision logic (no WiFi/MQTT/FreeRTOS calls), driven by a caller that feeds it a monotonic clock and connect/fail/disconnect events and asks it "what should I do now?". Lives in `include/`/`src/controller/` so it compiles into both firmware and the `native-tests` build. Later increments wire the firmware's WiFi/MQTT task to this policy.

**Tech Stack:** C++ (Arduino-ESP32 / native), PlatformIO `native-tests`, project `TEST_CASE`/`ASSERT_*` harness.

---

## File Structure
- **Create** `include/controller/network_recovery_policy.h` — `NetworkRecoveryConfig`, `RecoveryAction` enum, `NetworkRecoveryPolicy` class declaration.
- **Create** `src/controller/network_recovery_policy.cpp` — the logic (backoff + escalation state machine).
- **Create** `src/tests/test_network_recovery_policy.cpp` — native unit tests.

**Design contract (`NetworkRecoveryPolicy`):**
- Caller loop: each tick, call `RecoveryAction action = policy.poll(now_ms)`. Act on it (attempt a connect, restart the subsystem, or reboot), then report the outcome via `on_connected()` / `on_connect_failed()` / `on_disconnected()`.
- `RecoveryAction`: `None`, `Connect`, `RestartSubsystem`, `Reboot`.
- Policy:
  - From `Disconnected`/after a failure, `poll()` returns `Connect` once the backoff window has elapsed (else `None`).
  - Backoff is exponential from `base_backoff_ms`, doubling per consecutive failure, capped at `max_backoff_ms`.
  - After `fails_before_restart` consecutive failed connects, `poll()` returns `RestartSubsystem` (and the consecutive-fail counter resets, the restart counter increments).
  - After `restarts_before_reboot` subsystem restarts without ever reaching `Connected`, `poll()` returns `Reboot` (last resort).
  - `on_connected()` resets ALL counters and backoff (healthy).
  - `on_disconnected()` (was connected, link dropped) returns to the connect/backoff cycle but does NOT immediately escalate.
- Deterministic: no internal clock, no randomness in the core decision (jitter, if any, is added by the caller). The clock is always passed in.

---

## Task 1: Define the interface + config + action enum

**Files:** Create `include/controller/network_recovery_policy.h`

- [ ] **Step 1: Create the header**

```cpp
#pragma once

#include <stdint.h>

namespace thermostat {

struct NetworkRecoveryConfig {
  uint32_t base_backoff_ms = 1000;       // first retry delay
  uint32_t max_backoff_ms = 60000;       // backoff cap
  uint32_t fails_before_restart = 5;     // consecutive failed connects -> restart subsystem
  uint32_t restarts_before_reboot = 3;   // subsystem restarts w/o a success -> reboot (last resort)
};

enum class RecoveryAction : uint8_t {
  None = 0,            // wait (backoff not elapsed)
  Connect = 1,         // attempt a connection now
  RestartSubsystem = 2,// tear down + recreate the network subsystem (task/client)
  Reboot = 3,          // last resort
};

// Pure decision logic for recovering a network subsystem without rebooting.
// No WiFi/MQTT/FreeRTOS/clock calls — the caller passes a monotonic ms clock and
// reports outcomes. See the plan's design contract.
class NetworkRecoveryPolicy {
 public:
  explicit NetworkRecoveryPolicy(const NetworkRecoveryConfig &config = NetworkRecoveryConfig())
      : config_(config) {}

  // What to do now. Returns Connect at most once per backoff window; the caller
  // must then report on_connected()/on_connect_failed().
  RecoveryAction poll(uint32_t now_ms);

  void on_connected();        // healthy — reset all backoff/counters
  void on_connect_failed();   // a Connect attempt failed — advance backoff/escalation
  void on_disconnected();     // an established link dropped — resume connect cycle

  bool connected() const { return connected_; }
  uint32_t current_backoff_ms() const { return backoff_ms_; }
  uint32_t consecutive_fails() const { return consecutive_fails_; }
  uint32_t restart_count() const { return restart_count_; }

 private:
  NetworkRecoveryConfig config_{};
  bool connected_ = false;
  bool attempt_in_progress_ = false;  // a Connect was issued, awaiting outcome
  uint32_t consecutive_fails_ = 0;
  uint32_t restart_count_ = 0;
  uint32_t backoff_ms_ = 0;           // current wait between attempts
  uint32_t next_attempt_ms_ = 0;      // earliest time for the next Connect
  bool started_ = false;              // has poll() been called at least once
};

}  // namespace thermostat
```

- [ ] **Step 2: Verify it compiles**

Run: `pio run -e native-tests`
Expected: SUCCESS (header-only so far; just a syntax check).

- [ ] **Step 3: Commit**

```bash
git add include/controller/network_recovery_policy.h
git commit -m "feat(controller): add NetworkRecoveryPolicy interface (resilience increment 1)"
```

---

## Task 2: Implement the policy logic (TDD)

**Files:** Create `src/tests/test_network_recovery_policy.cpp`, then `src/controller/network_recovery_policy.cpp`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/test_network_recovery_policy.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/network_recovery_policy.h"
#include "test_harness.h"

using thermostat::NetworkRecoveryPolicy;
using thermostat::NetworkRecoveryConfig;
using thermostat::RecoveryAction;

static NetworkRecoveryConfig cfg() {
  NetworkRecoveryConfig c;
  c.base_backoff_ms = 1000;
  c.max_backoff_ms = 8000;
  c.fails_before_restart = 3;
  c.restarts_before_reboot = 2;
  return c;
}

TEST_CASE(recovery_first_poll_attempts_connect_immediately) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  // Second poll before any outcome: do not re-issue Connect.
  ASSERT_TRUE(p.poll(1) == RecoveryAction::None);
}

TEST_CASE(recovery_backoff_doubles_and_caps) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  p.on_connect_failed();
  // backoff = 1000 after first fail; no Connect until 1000 elapsed
  ASSERT_TRUE(p.poll(500) == RecoveryAction::None);
  ASSERT_TRUE(p.poll(1000) == RecoveryAction::Connect);
  ASSERT_EQ(p.current_backoff_ms(), 1000u);
  p.on_connect_failed();              // backoff -> 2000
  ASSERT_EQ(p.current_backoff_ms(), 2000u);
}

TEST_CASE(recovery_escalates_to_restart_then_reboot) {
  NetworkRecoveryPolicy p(cfg());  // fails_before_restart=3, restarts_before_reboot=2
  uint32_t t = 0;
  // Drive 3 consecutive failed connects -> RestartSubsystem on the 3rd.
  RecoveryAction last = RecoveryAction::None;
  int restarts = 0, reboots = 0, guard = 0;
  bool connected_ever = false;
  while (guard++ < 1000 && reboots == 0) {
    RecoveryAction a = p.poll(t);
    if (a == RecoveryAction::Connect) { p.on_connect_failed(); }
    else if (a == RecoveryAction::RestartSubsystem) { restarts++; }
    else if (a == RecoveryAction::Reboot) { reboots++; }
    t += 10000;  // advance well past any backoff each step
  }
  (void)last; (void)connected_ever;
  // After restarts_before_reboot (2) restarts with no success, it must reboot.
  ASSERT_EQ(restarts, 2);
  ASSERT_EQ(reboots, 1);
}

TEST_CASE(recovery_on_connected_resets_everything) {
  NetworkRecoveryPolicy p(cfg());
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  p.on_connect_failed();
  p.poll(2000); p.on_connect_failed();
  ASSERT_TRUE(p.consecutive_fails() > 0);
  p.on_connected();
  ASSERT_TRUE(p.connected());
  ASSERT_EQ(p.consecutive_fails(), 0u);
  ASSERT_EQ(p.restart_count(), 0u);
  ASSERT_EQ(p.current_backoff_ms(), 0u);
}

TEST_CASE(recovery_disconnect_resumes_connect_cycle_without_escalating) {
  NetworkRecoveryPolicy p(cfg());
  p.poll(0); p.on_connected();
  p.on_disconnected();
  ASSERT_TRUE(!p.connected());
  // Immediately eligible to reconnect, not escalated.
  ASSERT_TRUE(p.poll(0) == RecoveryAction::Connect);
  ASSERT_EQ(p.restart_count(), 0u);
}
#endif  // THERMOSTAT_RUN_TESTS
```

- [ ] **Step 2: Run — verify it fails to link**

Run: `pio run -e native-tests`
Expected: FAIL — `NetworkRecoveryPolicy::poll` / `on_*` undefined (no .cpp yet).

- [ ] **Step 3: Implement the logic**

Create `src/controller/network_recovery_policy.cpp`:

```cpp
#include "controller/network_recovery_policy.h"

namespace thermostat {

RecoveryAction NetworkRecoveryPolicy::poll(uint32_t now_ms) {
  if (!started_) {
    started_ = true;
    next_attempt_ms_ = now_ms;  // attempt immediately on first poll
  }
  if (connected_ || attempt_in_progress_) return RecoveryAction::None;

  // Escalation takes priority once we've exhausted reconnect attempts.
  if (consecutive_fails_ >= config_.fails_before_restart) {
    consecutive_fails_ = 0;
    ++restart_count_;
    if (restart_count_ > config_.restarts_before_reboot) {
      return RecoveryAction::Reboot;
    }
    // Restarting the subsystem: reset backoff and try again promptly.
    backoff_ms_ = 0;
    next_attempt_ms_ = now_ms;
    return RecoveryAction::RestartSubsystem;
  }

  // Time to attempt a connect?
  if (static_cast<int32_t>(now_ms - next_attempt_ms_) >= 0) {
    attempt_in_progress_ = true;
    return RecoveryAction::Connect;
  }
  return RecoveryAction::None;
}

void NetworkRecoveryPolicy::on_connected() {
  connected_ = true;
  attempt_in_progress_ = false;
  consecutive_fails_ = 0;
  restart_count_ = 0;
  backoff_ms_ = 0;
}

void NetworkRecoveryPolicy::on_connect_failed() {
  attempt_in_progress_ = false;
  ++consecutive_fails_;
  backoff_ms_ = (backoff_ms_ == 0) ? config_.base_backoff_ms : backoff_ms_ * 2;
  if (backoff_ms_ > config_.max_backoff_ms) backoff_ms_ = config_.max_backoff_ms;
  // next_attempt set relative to "now" by the caller's next poll via backoff:
  // we store the delay; poll() compares against next_attempt_ms_ which we advance here
  // using the last attempt time is unavailable, so advance from 0 baseline:
}

void NetworkRecoveryPolicy::on_disconnected() {
  connected_ = false;
  attempt_in_progress_ = false;
  // Resume the connect cycle promptly; do not escalate on a clean drop.
  consecutive_fails_ = 0;
  backoff_ms_ = 0;
}

}  // namespace thermostat
```

NOTE TO IMPLEMENTER: the backoff timing needs `next_attempt_ms_` to be advanced by `backoff_ms_` relative to the failure time. `on_connect_failed()` has no `now_ms`. Resolve this cleanly by having `poll()` own the scheduling: when an attempt fails, the NEXT `poll(now)` that sees `attempt_in_progress_ == false && consecutive_fails_ > 0 && backoff not yet scheduled` sets `next_attempt_ms_ = now + backoff_ms_`. Implement whichever shape makes the Task-2 tests pass with clean, readable code — the tests are the contract (immediate first attempt; wait `backoff_ms_` after a failure; doubling+cap; escalate to RestartSubsystem then Reboot; reset on connected; resume on disconnect). Adjust the `.cpp` (and only if necessary the private fields) to satisfy them; do not change the test expectations.

- [ ] **Step 4: Run — verify tests pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, count increased by 5.

- [ ] **Step 5: Commit**

```bash
git add include/controller/network_recovery_policy.h src/controller/network_recovery_policy.cpp src/tests/test_network_recovery_policy.cpp
git commit -m "feat(controller): implement NetworkRecoveryPolicy (backoff + escalate, native-tested)"
```

---

## Self-Review

**Spec coverage:** Implements the decision core of spec §3 "recover-without-reboot" + bounded backoff — the policy that later increments use to drive the WiFi/MQTT task so `esp_restart()` becomes the last resort, not the first. Pure logic, fully native-tested; no firmware wiring yet (that's increment 3/4).

**Placeholder scan:** The `.cpp` in Task 2 Step 3 intentionally flags the one scheduling detail for the implementer to finalize against the tests (the tests are the precise contract). Everything else is concrete.

**Type consistency:** `RecoveryAction`/`NetworkRecoveryConfig`/`NetworkRecoveryPolicy` and the `poll`/`on_connected`/`on_connect_failed`/`on_disconnected` signatures are identical across header, tests, and `.cpp`.

**Risk:** None to the live controller — this is a new, unused module. It becomes load-bearing only when wired into the network task (later increment), which will be validated on the Feather bench.
