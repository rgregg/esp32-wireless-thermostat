# Autonomous Bring-up Session — Decisions Log (2026-06-16)

Working autonomously on the Waveshare ESP32-S3-ETH-8DI-8RO (real controller board) on
the piserial5 bench while the user is away. Board is wired to NOTHING except the Orange
Pi (USB) + Ethernet, so relays/WiFi/Ethernet are all safe to exercise freely. WiFi:
`RaspberryPie-Guest` / `12345678`. Branch `feat/controller-esp32s3-port` (PR #33).

**This file records decisions + findings for review. Items marked 🔵 NEEDS USER REVIEW
are choices I made on the user's behalf that they should confirm.**

## Goals for the session (Plan 4 bring-up, validated on the real board)
1. `Pca9554RelayBackend` — real relay backend (verified mapping) + safe-init.
2. PCF85063 RTC driver — network-independent time.
3. W5500 Ethernet bring-up (Ethernet cable is connected — get an IP).
4. WiFi bring-up (guest WiFi).
5. Full controller firmware on the board (board config + all of the above).
6. Validate resilience increments 1–2 on the real board.
7. Coredump partition (16 MB) + capture.

## Decisions

### D1 — Relay terminal → HVAC function mapping  🔵 NEEDS USER REVIEW
The user didn't specify which relay terminal (0–7) wires to heat/cool/fan. Since the
board is unwired (bench), the assignment doesn't affect testing. **Decision:** default
`heat=relay0, cool=relay1, fan=relay2, spare=relay3` (relays 4–7 unused), made
**configurable** in `Pca9554RelayBackendConfig`. **User: confirm the real wiring before
cutover** — if the furnace's W/Y/G land on different relay terminals, change the config
(one line) to match; no code change.

## Findings

### F1 — Relay control path VALIDATED on real hardware ✅
`ControllerRelayIo` (interlock) + `Pca9554RelayBackend` drive the real relays correctly
(serial trace via piserial5): HEAT→relay0 (PCA 0x01), switch to COOL→interlock forces
all-off + ~1s wait→relay1 (0x02), switch to FAN→off+wait→relay2 (0x04), OFF→0x00. The
Plan-1 abstraction + Plan-4 PCA9554 backend + the interlock all work end-to-end on the
Waveshare board. Default mapping heat=0/cool=1/fan=2 confirmed (see D1 for wiring review).

### F2 — On-board BUZZER (GPIO46) is uncontrolled at boot — must be silenced  🔵 IMPORTANT FOR REAL FIRMWARE
The board has a buzzer on GPIO46. With the pin left uninitialized at boot it can sound
(the user heard an "alarm"). **The real controller firmware MUST drive GPIO46 to its
off level (LOW) early in setup()** so the board is silent on boot. Added to the bench
sketches; needs to be in the production controller-s3 board init. (If LOW doesn't
silence it, the buzzer is active-low and needs HIGH — confirm on hardware.)
Possible future use: deliberate audible alerts (e.g. failsafe), but default = silent.

### F3 — PCF85063 RTC VALIDATED on real hardware ✅ + battery finding  🔵 USER: consider RTC battery
RTC driver read/set/tick confirmed on the board: read-before showed osc_ok=0 (time lost),
set 2026-06-16 12:00:00 -> osc_ok=1, seconds advance (oscillator runs). Decode/encode +
osc-stop flag all correct. **Finding:** the RTC backup (coin cell/supercap) is NOT
populated/charged (osc_ok=0 at boot = time not retained across power-off). The firmware
handles this (trust RTC only when osc_ok, else NTP), but **populating the RTC battery
would give instant correct time on cold boot** — recommend adding it for the cutover unit.

### F4 — Pca9554RelayBackend + PCF85063 RTC drivers: native-tested (176) + on-board validated.

## Remaining integration analysis (for the full controller firmware on the Waveshare)

### F5 — Controller has NO wall-clock time today  🔵 NEEDS USER INPUT (scheduling)
Grep confirms the controller never calls configTime/SNTP/getLocalTime — it has no
wall-clock time at all currently (only the thermostat `runtime()`). So the RTC is a
NEW capability. I'll wire the modest plumbing autonomously (read RTC on boot ->
settimeofday; SNTP sync -> write back to RTC; accurate log timestamps + an `rtc_ok`
diagnostic). But **what to DO with the time (time-of-day HVAC scheduling) is a product
decision** — what schedule, configured how (HA? on-device?) — so I'm NOT inventing a
scheduling feature autonomously. Flagging for review.

### Remaining Plan-4 integration increments (sized; doing the safe ones autonomously)
- **Board config (small, safe — doing it):** `CONTROLLER_BOARD_S3` currently uses the
  Feather stand-in `GpioRelayBackend{4,5,6,7}`. Switch it to `Pca9554RelayBackend`
  (verified mapping) + silence GPIO46 buzzer in setup() + Waveshare I2C pins. This makes
  the controller-s3 firmware target the real board's relays. Validate it builds + drives
  relays on the board.
- **RTC plumbing (medium — doing the plumbing, not scheduling):** read RTC on boot ->
  seed system time; SNTP -> RTC writeback; log timestamps. (Scheduling deferred, see F5.)
- **Ethernet-for-IP + WiFi-for-ESP-NOW-only (large — spec §2, needs care + likely user
  review):** the networking-model refactor (IP-link abstraction: Ethernet primary, WiFi
  radio only for ESP-NOW). This is the biggest remaining piece and changes the live
  network path substantially — I'll bring up Ethernet standalone first (validating now),
  then propose the integration rather than rewrite the live network path unsupervised.
- **Coredump (16MB partition) + identity override + cutover** — later.
