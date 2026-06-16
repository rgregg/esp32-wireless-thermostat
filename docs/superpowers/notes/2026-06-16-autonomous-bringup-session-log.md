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
(appended as the session progresses)
