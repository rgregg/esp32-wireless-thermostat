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
- [ ] CI pipeline for required build/test matrix (status: In Progress)
  - Acceptance:
    - Runs the 4 required commands on every push/PR.
    - Fails fast and reports which env/test failed.
  - Evidence: workflow file at `.github/workflows/ci.yml` + passing run link/screenshot in PR.

- [ ] Management/config integration tests (status: To Do)
  - Acceptance:
    - Covers MQTT config set/state routing for controller and thermostat topics.
    - Covers controller proxy updates for display config over MQTT.
    - Covers redaction behavior for password/secret fields in `/config` and state topics.
  - Evidence: new host tests in `src/tests/*` and passing `native-tests`.

- [ ] ESP-NOW protocol robustness tests (status: To Do)
  - Acceptance:
    - Invalid packet length/type/version is safely rejected.
    - Sequence stale/duplicate/replay edge cases are covered.
    - Peer MAC filtering behavior is covered for unicast and broadcast fallback modes.
  - Evidence: dedicated tests + passing `native-tests`.

- [ ] Config contract freeze (status: To Do)
  - Acceptance:
    - Every runtime config key documented with: key name, type, default, range, reboot-required.
    - Controller and thermostat keys separated clearly.
  - Evidence: section added in `docs/deployment-runbook.md`.

### P1
- [ ] Release artifact script and verification (status: To Do)
  - Acceptance:
    - One command builds both firmware binaries and records firmware version string from git.
    - Artifact naming includes env + version.
  - Evidence: script path + usage documented.

- [ ] OTA rollout + rollback playbook (status: To Do)
  - Acceptance:
    - Step-by-step rollout order (controller/display), health checks, and rollback triggers.
    - Includes recovery path when one node is unreachable.
  - Evidence: runbook section updated.

- [ ] Health/diagnostic telemetry parity pass (status: To Do)
  - Acceptance:
    - Topics for uptime, RSSI, free heap, and last comms timestamps are defined and emitted.
    - Error reason topic(s) defined for MQTT/OTA/ESP-NOW failures.
  - Evidence: code + topic list in docs.

### P2
- [ ] Fault-injection harness for transport/runtime boundaries (status: To Do)
  - Acceptance:
    - Simulates transient drops/timeouts/out-of-order bursts in host tests.
  - Evidence: repeatable test harness + tests.

## Current Sprint (Pre-Hardware Sprint 1)
- [x] Land CI workflow for required matrix.
- [ ] Land first config integration test batch.
- [ ] Land first ESP-NOW robustness test batch.
- [ ] Update deployment runbook with config contract table.
