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

**Waveshare ESP32-S3-ETH-8DI-8RO-C** — ESP32-S3-WROOM-1U **N16R8** (CONFIRMED on
hardware: 16 MB flash, 8 MB PSRAM, MAC 3c:0f:02:cc:ee:2c).

- **Relays (8):** PCA9554 I²C expander @ `0x20`, **active-high** — **VERIFIED on
  hardware** (bring-up: relay N = bit `1<<N`; boot-safe state confirmed off). I²C `SDA=42, SCL=41`.
- **RTC:** **PCF85063** @ I²C `0x51` (same bus) — **found on hardware.** Battery/supercap
  backed timekeeping; see §7.
- **Ethernet:** W5500 over SPI — `CLK=15, MOSI=13, MISO=14, CS=16, INT=12` (RST: verify on hardware).
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

## Build configuration (one codebase, two boards)

Board differences are isolated behind compile-time-selected backends; everything
else is shared. Selected via PlatformIO env + build flag (e.g. `-D CONTROLLER_BOARD_S3`).

| Concern | `esp32-furnace-controller` (old, esp32dev / 4 MB) | `esp32-furnace-controller-s3` (new, S3 / 16 MB) |
|---|---|---|
| Relay backend | `GpioRelayBackend` (direct GPIO) | `Pca9554RelayBackend` (I²C @0x20) |
| IP link | `WiFiStation` backend (associates to AP) | `Ethernet` (W5500) backend |
| ESP-NOW | WiFi radio, AP-driven channel | WiFi radio, no association, pinned channel |
| Partitions | `partitions_no_spiffs.csv` (4 MB) | custom 16 MB + coredump |
| Panic-PC breadcrumb (RTC) | available | available |
| Full coredump (flash) | unavailable (no partition) | enabled |
| **Shared** | control loop, thermostat logic, resilience refactor, ESP-NOW protocol, MQTT/HA discovery, weather, relay interlock logic (`ControllerRelayIo`) | same |

Note: the networking *model* (Ethernet-for-IP + WiFi-for-ESP-NOW-only) applies to
the **new** board only; the old board has no Ethernet and necessarily keeps
WiFi-station association for IP. The `IP-link` abstraction (§2) resolves to
`WiFiStation` on the old board and `Ethernet` on the new one, keeping the MQTT/
weather layers board-agnostic.

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

### 4. Crash diagnostics (unified — three layers)
A single crash-visibility story, layered by what each board can hold:

1. **`wdt_section` breadcrumb (existing, both boards):** names the instrumented
   blocking section (weather geocode/forecast, mqtt connect, mdns) active at a
   watchdog reset. RTC_NOINIT memory.
2. **Panic-PC breadcrumb (new, both boards):** a custom panic-handler hook stashes
   the **faulting PC + a few backtrace addresses** into RTC_NOINIT memory (survives
   panic/reset, same mechanism as the existing breadcrumb), published on next boot
   via a new `state/panic_pc` topic + HA diagnostic sensor, then cleared. Resolve
   offline with `addr2line` against the matching build `.elf` (rebuildable from the
   firmware_version git sha). Gives the **old 4 MB board** crash *localization*
   without needing a coredump partition.
3. **Full coredump (new board only):** custom 16 MB partition table with a
   dedicated coredump partition; `esp_core_dump` to flash, panic-triggered. On boot
   read the summary (PC + backtrace + registers/stack), publish over MQTT
   (`state/coredump`), then clear. The complete dump the 4 MB board can't hold.

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
  parsing; panic-PC breadcrumb encode/decode/format; **RTC time encode/decode (BCD)**.
- **Hardware validation checklist (Phase 2):** relay **safe state at power-on**
  (multimeter COM→NO before wiring to HVAC), Ethernet link + DHCP, ESP-NOW channel
  match with display, coredump capture→publish→clear round-trip, OTA on the new
  partition layout, full HVAC cycle (heat/cool/fan) with interlocks, **RTC read/set +
  survives a power-cycle (if battery-backed)**.

### 7. RTC — network-independent time (PCF85063 @ I²C 0x51)
The board has a **PCF85063A** RTC on the relay I²C bus. This is a genuine resilience
win: **the controller keeps accurate wall-clock time even with the network down**, so
time-dependent behavior doesn't depend on NTP/SNTP reachability.

- **Driver:** a small PCF85063 driver — read/set time over I²C. The **BCD ↔ struct
  encode/decode is platform-agnostic and unit-tested** (like the relay bit-logic);
  only the `Wire` transport is firmware. Also read the **oscillator-stop / low-voltage
  flag** (RTC reg) to know whether the held time is trustworthy (battery was lost).
- **Sync strategy — RTC is the local source of truth, NTP corrects it:**
  - On boot, read the RTC immediately → the controller has a valid time before any
    network comes up (no "1970" window, no waiting on SNTP).
  - When network time is available (SNTP sync), **write it back to the RTC** so the RTC
    stays accurate and survives the next reboot/outage.
  - System time (`settimeofday`) is seeded from the RTC at boot and re-disciplined by
    SNTP; the RTC is re-written on each successful SNTP sync (and periodically).
- **Uses:** time-of-day HVAC scheduling without NTP; accurate **log/diagnostic
  timestamps from boot** (incl. reboot-cause/breadcrumb timing); runtime accounting
  anchored to real time. Optionally publish `state/rtc_time` + an `rtc_ok` diagnostic.
- **Battery/backup:** the RTC only retains time across a full power-off if the board's
  RTC backup (coin cell / supercap) is populated — **verify/populate it** for cold-boot
  benefit; without it the RTC still helps across brownouts/resets and gets corrected by
  SNTP. The osc-stop flag tells us at boot whether to trust the held time.
- **Display-agnostic:** controller-only (the display gets time over ESP-NOW/MQTT as today).

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
