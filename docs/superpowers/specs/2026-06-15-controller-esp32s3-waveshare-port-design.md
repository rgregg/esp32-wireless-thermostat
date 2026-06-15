# Controller Port → Waveshare ESP32-S3-ETH-8DI-8RO — Design Spec

**Date:** 2026-06-15
**Branch:** `feat/controller-esp32s3-port`
**Status:** Approved design → implementation planning

## Goal

Port the furnace **controller** firmware from the current classic-ESP32 board
(`esp32dev`, 4 MB) to the Waveshare **ESP32-S3-ETH-8DI-8RO** (CAN/PoE variant),
and use the move to fix the chronic instability we've been fighting:

- **More flash (16 MB)** → a dedicated coredump partition, so we can finally
  capture the `panic` backtrace we currently can't see (no serial, 4 MB full).
- **Wired Ethernet** → remove the marginal-WiFi link that drives the
  `task_wdt` / weather-wedge failures (controller RSSI seen as low as −92 dBm).
- **Resilience refactor** → make the furnace control path independent of the
  network so network trouble can never stop heating/cooling or crash the device.

The display is **out of scope** except for one required change (ESP-NOW peer +
channel, see §6).

## Confirmed decisions

| Decision | Choice |
|---|---|
| Scope | Port **+ coredump + resilience** (all three) |
| IP transport | **Ethernet for IP** (MQTT/HA/weather); WiFi radio used **only for ESP-NOW**, no AP association |
| HA identity | **Preserve** — override discovery device id so HA sees the same controller device |
| Digital inputs (8× DI) | **Reserved, unused** this pass (YAGNI) |
| Old board | **Kept building** via a relay-backend abstraction (working fallback throughout) |

## Target hardware

**Waveshare ESP32-S3-ETH-8DI-8RO-C** — ESP32-S3-WROOM-1U **N16R8** (16 MB flash,
8 MB PSRAM). Pin map (from vendor/community docs; **re-verify against the exact
SKU when hardware arrives** — the CAN variant may differ on the RS485/CAN bus,
but relay/Ethernet/DI pins are the shared base):

- **Relays (8):** PCA9554 I²C expander @ `0x20`, pins 0–7, **active-high**. I²C `SDA=42, SCL=41`.
- **Ethernet:** W5500 over SPI — `CLK=15, MOSI=13, MISO=14, CS=16, INT=12` (RST: verify).
- **Digital inputs (8):** GPIO 4–11, `INPUT_PULLUP`, inverted, debounced (reserved, unused).
- **Misc:** RGB LED (WS2812) GPIO38, buzzer GPIO46, boot button GPIO0.
- **Power:** 7–36 V DC screw terminal, USB-C (power/flash/debug), PoE (this SKU).

## Approach

**Phased, single branch, behind a board abstraction.**

- **Phase 1 (no hardware):** resilience refactor + relay-backend abstraction +
  coredump scaffolding, developed and **unit-tested in native**. The current
  `esp32dev` controller keeps building on the GPIO backend and gains the
  resilience improvements immediately.
- **Phase 2 (on hardware):** new `esp32-furnace-controller-s3` env — PCA9554
  relay backend, W5500 Ethernet, fixed-channel ESP-NOW, coredump partition,
  identity override. Validate on the bench, then swap per the flashing runbook.

Rejected: S3-only hard switch (no fallback, can't land resilience on the current
board now); separate branch per subsystem (resilience and Ethernet both touch the
network layer → merge friction).

## Design

### 1. Hardware abstraction — relays
- Introduce a `RelayBackend` interface; `ControllerRelayIo` keeps **all** its
  interlock / force-off / pending logic unchanged and calls the backend for
  `begin()` + `write_outputs()`.
- `GpioRelayBackend` — current board (direct GPIO, existing behavior).
- `Pca9554RelayBackend` — I²C @ `0x20`, active-high. **Safe init: write output
  register `0x00` BEFORE setting the config register to outputs**, so the
  input→output transition cannot momentarily energize relays (PCA9554 output
  reg POR default is `0xFF`).
- Byte-packing (relay index → bit in the output byte) extracted to a
  platform-agnostic, **unit-tested** header (per the sim/firmware split); the
  `Wire` transport stays in firmware.
- Backend selected by build flag / PlatformIO env.

### 2. Networking
- **Ethernet (W5500)** is the IP path for MQTT/HA/weather. A small **IP-link
  abstraction** lets MQTT/weather code be link-agnostic.
- **WiFi radio** is initialized in a **no-association mode pinned to a fixed
  ESP-NOW channel** (no STA join). This removes the flaky WiFi-association layer
  entirely while keeping the local ESP-NOW link to the display.
- ⚠️ **ESP-NOW channel-coordination risk:** ESP-NOW requires controller and
  display on the same radio channel. Today they match because both associate to
  the same AP. Once the controller stops associating and pins a channel, the
  **display's AP must use that channel, or the display must pin the same channel**
  (small display-side follow-up). The controller will expose the ESP-NOW channel
  as config; verify channel match at cutover. Mitigation if mismatch is a problem:
  pin the display too / move display to Ethernet later.

### 3. Resilience refactor
- **Control loop** runs at a fixed cadence with **zero network dependency**
  (read indoor temp via ESP-NOW → compare setpoint → drive relays).
- **Move MQTT connect/publish off the main path** into a dedicated network task
  (weather already is). The main loop never blocks on the network.
- **Replace `esp_restart()` recoveries with subsystem restarts.** Use a
  self-terminating network task (checks a flag and returns cleanly so TLS objects
  run their destructors) instead of `vTaskDelete` — so we recover networking
  without rebooting. Reserve reboot for truly unrecoverable states, and avoid
  rebooting mid HVAC cycle where possible.
- **Degradation tiers:** control > ESP-NOW > MQTT/HA > weather. A failure in any
  tier must not affect the tiers below it.
- **Bounded everything** (carry forward PR #32: connect/handshake/read timeouts,
  task-WDT, MQTT socket timeout) + backoff with jitter + heap budget under failure.
- **Relay fault policy:** the PCA9554 **latches relay state across a CPU reset**
  (it doesn't reset when the ESP32 does) — deliberately chosen so a watchdog
  reboot doesn't drop an active heat/cool call. Document and make explicit.

### 4. Coredump
- Custom **16 MB partition table** with a dedicated **coredump partition**
  (plus app/OTA/NVS). Enable `esp_core_dump` to flash, panic-triggered.
- On boot, read the coredump **summary** (faulting PC + backtrace), **publish it
  over MQTT** alongside `wdt_section` (new `state/coredump` topic + HA diagnostic
  sensor), then clear the partition. This finally surfaces the panic backtrace.

### 5. Identity & migration
- **Override the discovery device id / base** so HA continues to see the same
  controller device — existing automations, the reboot alert, history, and
  dashboards keep working with no entity_id rework.
- **Update the display's ESP-NOW peer** to the new controller MAC (and confirm
  channel per §2).
- **Cutover:** bench-flash the new board → validate (see §6) → HVAC-off + 60 s
  settle → physically swap → verify. Old board retained as rollback.

### 6. Testing
- **Native unit tests:** relay bit-logic; resilience state machines (link up/down,
  network-task restart, degradation tiers); IP-link selection; coredump-record
  parsing.
- **Hardware validation checklist (Phase 2):** relay **safe state at power-on**
  (multimeter COM→NO before wiring to HVAC), Ethernet link + DHCP, ESP-NOW channel
  match with display, coredump capture→publish→clear round-trip, OTA on the new
  partition layout, full HVAC cycle (heat/cool/fan) with interlocks.

## Out of scope
- Digital-input features (reserved, unused).
- RS485/CAN bus usage.
- Moving the display to Ethernet (possible later follow-up for ESP-NOW channel).
- PoE infrastructure (board supports it; power method is an install choice).

## Risks
- **ESP-NOW channel mismatch** once the controller stops associating (see §2) —
  highest-attention item at cutover.
- **Exact SKU pin differences** (CAN variant) — re-verify pin map on arrival.
- **PCA9554 power-on / pre-I²C window** — board pull resistors decide raw
  power-on relay state; confirm via schematic + bench before HVAC wiring.
- Panic root cause still unknown (post-deploy `reset_reason=panic`, `wdt_section=none`,
  no backtrace available); the coredump partition is the instrument to find it, not
  a guaranteed fix.
