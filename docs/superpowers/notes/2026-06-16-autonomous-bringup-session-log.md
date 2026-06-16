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

### F6 — W5500 wired Ethernet VALIDATED on real hardware ✅  (the core value prop)
`ETH.begin(ETH_PHY_W5500, phy_addr=1, CS=16, IRQ=12, RST=-1, SPI2_HOST, SCK=15, MISO=14,
MOSI=13)` returns ok; link comes up and **DHCP gets an IP**: serial trace showed
`linkUp=1 IP=10.0.2.43 speed=100Mbps FD mac=3A:0F:02:CC:EE:2D` (took ~6s from boot to
lease). RST pin = **-1 works** (no dedicated reset needed; W5500 self-resets). This is
the whole reason for the port — a wired link to end the marginal-WiFi reboots — and it
works on the real board. Bench sketch `src/bench/waveshare_eth_validate.cpp`, env
`waveshare-eth-validate` (hybrid build — the ETH/Network stack needs the IDF source
rebuild, same as the panic-wrap path). Note: bench LAN handed out 10.0.2.x; production
will be on the user's normal LAN. MAC is chip-MAC+derived (locally administered).

## Board-config integration (Plan 4) — full controller firmware targets the real board

### What I did (committed, building/validating)
Added a **`CONTROLLER_BOARD_WAVESHARE`** board profile to the real controller firmware
(`src/esp32_controller_main.cpp`), distinct from the generic-S3 bench profile:
- Relay backend: `Pca9554RelayBackend` (verified mapping heat0/cool1/fan2/spare3) instead
  of the Feather `GpioRelayBackend` stand-in. Mutually exclusive via `#elif`.
- Buzzer: `pinMode(46,OUTPUT); digitalWrite(46,LOW);` as the **first** statement in
  setup() (before Serial), gated to the Waveshare board.
- New env `[env:esp32-furnace-controller-waveshare]` — extends the s3 hybrid env, 16MB
  flash + `default_16MB.csv` (which already carries a coredump partition), `-DCONTROLLER_BOARD_WAVESHARE`.

### Scope boundary (deliberate — NOT done autonomously)
This makes the firmware **build for + boot on** the real board with correct relay + buzzer
hardware. It does **NOT** yet rewire the network path to Ethernet — main still uses WiFi
for IP today (the Ethernet-for-IP refactor is the large §2 piece flagged for review). So
on the bench this firmware boots, drives the real relays safe-off, stays silent, and (with
no WiFi creds) sits in provisioning — exactly the boot/hardware checkpoint intended. Kept
the **isolated TEST identity** (test base topic + ESP-NOW ch11/test-LMK) so it can never
touch production until the cutover step.

### F7 — Full controller firmware BOOTS CLEAN + STABLE on the real board ✅
Flashed `esp32-furnace-controller-waveshare` (1.48MB, 22.6% of app slot, 21.2% RAM) to
the real Waveshare board and read the boot:
- buzzer SILENT (GPIO46 LOW ran first — no alarm);
- `panic_pc=none wdt_section=none`, `Reset reason: other | last reboot cause: none` (clean);
- `controller_node_begin=1` → `g_relay_io.begin()` → `Pca9554RelayBackend.begin()` found the
  expander and set relays safe-off with NO I2C hang/error (begin() contract holds on real HW);
- isolation watchdog armed; `controller alive` heartbeat every 5s for 40s — **no panic, no
  watchdog reset, no reboot**: stable.
- Correctly entered BLE provisioning (fresh NVS, no WiFi creds) — confirms Ethernet is NOT
  yet wired into main (still WiFi-for-IP). NVS NOT_FOUND lines = expected first-boot noise.
Milestone: the real production firmware runs on the real board driving the real relay
hardware, silent and stable. Plan-4 hardware bring-up is functionally COMPLETE.

### F8 — Crash COREDUMP capture VALIDATED end-to-end on real hardware ✅  (closes panic-diagnostics gap)
Built `waveshare-coredump-validate` (sketch deliberately crashes, reboots, then checks
`esp_core_dump_image_get()`). On the real board:
- boot1: no coredump -> 8s countdown -> null-ptr write -> `Guru Meditation StoreProhibited`,
  `EXCVADDR=0x00000000`, PC `0x42002011`; IDF wrote the coredump + rebooted;
- boot2: **`[coredump] PRESENT in flash: addr=0x00FF0000 size=12100 bytes`** — address matches
  the `coredump` partition in default_16MB.csv exactly; CAPTURE VALIDATED; erased clean.
- **Coredump is enabled by default** in the Arduino-ESP32 framework sdkconfig
  (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `DATA_FORMAT_ELF=y`, `CHECK_BOOT=y`) — so the
  ONLY thing needed was a partition table with a `coredump` partition (already in the 16MB
  layout / waveshare env). No sdkconfig change required.
- **Symbolication works**: addr2line on the env's firmware.elf resolved PC `0x42002011` ->
  `waveshare_coredump_validate.cpp:73` (the exact crash line) with a clean backtrace.
- Host-side full-coredump decode (`esp-coredump info_corefile`) needs xtensa-esp32s3-elf-gdb,
  which isn't installed on this dev box (and piserial5 is offline). Not blocking: the
  on-serial register dump + addr2line give the same symbolication, and the coredump is now
  captured in flash for offline `esp-coredump`/gdb analysis whenever needed.
**Impact:** directly closes the open "controller panics on new firmware, no coredump/backtrace
available" issue — once the Waveshare firmware is deployed, that panic WILL be captured.

## Code review of this session's firmware changes (esp-idf-engineer subagent)
Reviewed the board-config integration + bench sketches + envs. **No critical issues.**
Verified correct: macro mutual-exclusion (WAVESHARE/S3/classic — the only two
CONTROLLER_BOARD_S3 sites are both WAVESHARE-aware), buzzer placement, `extends`+override
semantics with all parent build_flags correctly re-listed, isolated test identity, and the
coredump API usage. Two "important" items + dispositions:
- **(a) PSRAM not enabled** despite N16R8's 8MB. **Disposition: intentional** — firmware fits
  in internal SRAM (~21%); enabling octal PSRAM needs pin-collision verification (W5500
  SPI/PCA9554 I2C/relays). Clarified the env comment to say so explicitly.
- **(b) Verify PCA9554 begin() doesn't briefly energize relays at boot** (highest-consequence
  for a furnace). **Disposition: already hardware-validated** — F1 showed `begin()` → PCA
  input 0x00 (all off); the backend writes the output register all-OFF *before* switching
  pins to outputs (POR default is outputs-high), and F7's clean boot confirmed no glitch.
Nits (not actioned): build_flag duplication across s3/waveshare envs (hoist to a common
section only if edited often); W5500 RST=-1 is bench-only (wire a real RST for production
warm-reboot link recovery); buzzer-silence snippet duplicated across 3 bench sketches.

## Wrap-up — what's DONE vs. what needs Ryan

### DONE autonomously this session (all committed + pushed to PR #33)
- ✅ Relays (Pca9554RelayBackend control path), RTC (PCF85063), W5500 Ethernet (DHCP),
  buzzer silence — all validated on the REAL board.
- ✅ `CONTROLLER_BOARD_WAVESHARE` firmware profile + env; full controller firmware boots
  clean + stable on the real board (relays safe, silent, no reboot).
- ✅ Coredump capture validated end-to-end (panic → coredump in flash → readable) +
  addr2line symbolication. Closes the open "no coredump" panic-diagnostics gap.
- ✅ Regression-checked: native tests 176/0; classic esp32dev controller still builds clean.
- ✅ Code-reviewed (esp-idf-engineer), no critical issues.

### NEEDS RYAN (recorded, not done — by design)
1. **🔵 D1 — relay→HVAC wiring:** confirm which relay terminal (0–7) is heat/cool/fan before
   cutover (one-line config if different from default 0/1/2).
2. **🔵 F3 — RTC battery:** consider populating the RTC backup cell for cold-boot time.
3. **🔵 F5 — RTC scheduling:** product decision (what to DO with wall-clock time). Plumbing
   I'll do; scheduling I won't invent.
4. **Ethernet-primary networking plan** — written:
   `docs/superpowers/plans/2026-06-16-ethernet-primary-networking.md`. Has 4 OPEN DECISIONS
   (WiFi fallback? watchdog reboot policy? keep BLE provisioning? static IP/DHCP reservation?)
   that need your answer before I execute it. Good news: the refactor is smaller than feared
   (lwIP already makes the MQTT/web/OTA clients link-agnostic).
5. **Resilience inc 1–2 on-device validation** — needs a controllable MQTT-unreachable bench
   (and, for the Ethernet plan, a broker reachable on the Ethernet LAN).
6. **Identity override + production cutover.**

## Ethernet-primary networking — IMPLEMENTED + VALIDATED on hardware ✅ (Ryan's decisions applied)
Executed `docs/superpowers/plans/2026-06-16-ethernet-primary-networking.md` after Ryan
confirmed: **no WiFi fallback** + **watchdog reboots only on isolation**. All changes gated
to `CONTROLLER_BOARD_WAVESHARE` (classic esp32dev untouched).
- Added `ctrl_ip_link_up()` / `ctrl_ip_local_addr()` helpers — the only place that knows the
  primary link. On Waveshare → Ethernet (`ETH.hasIP()`/`ETH.localIP()`); on classic → WiFi
  STA (reduces to the exact prior behavior, zero change).
- `ETH.begin(W5500, ...)` in setup() (Waveshare); ETH GOT_IP/DISCONNECTED handled in the
  net event handler. WiFi-creds provisioning **skipped** on Waveshare (WiFi = ESP-NOW only).
- Replaced the WiFi-status gates (web, mDNS, MQTT-connect, weather, announce×2, heartbeat,
  status JSON, OTA rollback) with the link helpers. RSSI stays WiFi-specific (0/omitted on
  Ethernet-only — correct).
- loop(): `ctrl_ensure_wifi_connected` + `wifi_watchdog_tick` skipped on Waveshare; the
  isolation watchdog (MQTT && ESP-NOW down >15min) is the SOLE reboot authority.

### F9 — on-board validation (fresh NVS, Ethernet to the bench LAN)
- `[eth] ETH.begin() -> ok` → `[eth] GOT_IP 10.0.2.43 link=100Mbps FD`.
- **No BLE provisioning** (`prov=idle`) — correctly skipped; `controller_node_begin=1`
  (ESP-NOW up with WiFi radio, no association).
- Heartbeat `ip=10.0.2.43, mqtt=yes` — **MQTT connected OVER ETHERNET** to the default broker
  `mqtt.lan` (resolved via DNS over the W5500), under the isolated TEST base topic. Proves
  the full DHCP→DNS→TCP→MQTT stack works over Ethernet with ZERO WiFi association — the
  link-agnostic-client theory holds exactly as planned.
- Stable for 30s+, no reboot. Builds: Waveshare clean (RAM dropped 21.2%→19.8% since WiFi
  provisioning no longer runs); native tests 176/0.
**This is the core deliverable of the whole port — the controller now runs on a wired link,
ending the marginal-WiFi reboot problem that started this work.**

### Code review of the Ethernet-primary refactor (esp-idf-engineer)
PASS overall: classic-path invariance confirmed (helpers reduce to the exact prior
WiFi.status()/localIP() behavior), String temporaries safe in every printf callsite,
ESP-NOW/ETH init ordering correct, `provisioning_active()` safe on the never-begun manager,
no new unused-var warnings. One **important** finding actioned:
- **3a — masked ETH.begin() failure / no retry:** a transient W5500 init failure would leave
  the board permanently IP-dark, and because ESP-NOW still works the isolation watchdog
  wouldn't catch it (not "isolated") → no auto-recovery. **Fix:** bounded 3× retry of
  ETH.begin() at boot (recovers transient SPI/PHY failures); if all fail, run ESP-NOW-only
  rather than reboot-loop on dead hardware (respects "reboot only on isolation").
- **nit 5 (actioned):** added a comment at the loop's WiFi-gate documenting the
  "stray wifi_ssid write persists creds but never associates (ensure_connected is gated out)
  → ESP-NOW channel pinning stays safe" invariant.
Other nits (heartbeat double-eval; announce-IP freshness self-heals on MQTT reconnect after a
flap-with-new-IP) noted, no action needed.
