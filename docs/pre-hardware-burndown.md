# Pre-Hardware Burn-Down Checklist

Goal: maximize confidence before physical hardware iteration starts.

## How To Use
- Keep this as the single checklist for all pre-hardware readiness tasks.
- Move items from `To Do` -> `In Progress` -> `Done`.
- Every completed item must include evidence (test name, command, doc path, or commit).

## Exit Criteria (Ready For Hardware Iteration)
- All `P0` tasks are `Done`.
- No open `P1` tasks that block first bring-up.
- CI is green on:
  - `pio run -e native-tests`
  - `./.pio/build/native-tests/program`
  - `pio run -e esp32-furnace-controller`
  - `pio run -e esp32-furnace-thermostat`
- Deployment and recovery runbook paths are documented and reviewed.

## Board

### P0
- [x] CI pipeline for required build/test matrix (status: Done)
  - Acceptance:
    - Runs the 4 required commands on every push/PR.
    - Fails fast and reports which env/test failed.
  - Evidence: workflow file at `.github/workflows/ci.yml`.

- [x] Management/config integration tests (status: Done)
  - Acceptance:
    - Covers MQTT config set/state routing for controller and thermostat topics.
    - Covers controller proxy updates for display config over MQTT.
    - Covers redaction behavior for password/secret fields in `/config` and state topics.
  - Evidence: expanded host tests in `src/tests/test_management_paths.cpp`; passing `native-tests` (28 tests).

- [x] ESP-NOW protocol robustness tests (status: Done)
  - Acceptance:
    - Invalid packet length/type/version is safely rejected.
    - Sequence stale/duplicate/replay edge cases are covered.
    - Peer MAC filtering behavior is covered for unicast and broadcast fallback modes.
  - Evidence: dedicated tests in `src/tests/test_codec.cpp` and `src/tests/test_espnow_packets.cpp`; passing `native-tests`.

- [x] Config contract freeze (status: Done)
  - Acceptance:
    - Every runtime config key documented with: key name, type, default, range, reboot-required.
    - Controller and thermostat keys separated clearly.
  - Evidence: runtime config contract section added in `docs/deployment-runbook.md`.

### P1
- [x] Release artifact script and verification (status: Done)
  - Acceptance:
    - One command builds both firmware binaries and records firmware version string from git.
    - Artifact naming includes env + version.
  - Evidence: `scripts/build_release_artifacts.sh`; usage documented in `docs/deployment-runbook.md`.

- [x] OTA rollout + rollback playbook (status: Done)
  - Acceptance:
    - Step-by-step rollout order (controller/display), health checks, and rollback triggers.
    - Includes recovery path when one node is unreachable.
  - Evidence: OTA rollout section in `docs/deployment-runbook.md`.

- [x] Health/diagnostic telemetry parity pass (status: Done)
  - Acceptance:
    - Topics for uptime, RSSI, free heap, and last comms timestamps are defined and emitted.
    - Error reason topic(s) defined for MQTT/OTA/ESP-NOW failures.
  - Evidence: telemetry/error topics in `docs/deployment-runbook.md`; emitted in controller and thermostat MQTT state publishing paths.

### P2
- [ ] Fault-injection harness for transport/runtime boundaries (status: To Do)
  - Acceptance:
    - Simulates transient drops/timeouts/out-of-order bursts in host tests.
  - Evidence: repeatable test harness + tests.

## Current Sprint (Pre-Hardware Sprint 1)
- [x] Land CI workflow for required matrix.
- [x] Land config integration test coverage (including secret key/redaction classification).
- [x] Land ESP-NOW robustness test coverage.
- [x] Update deployment runbook with config contract table.
