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
