# Resilience Increments 3 & 4 — Design

> **Status: DRAFT for review.** Touches the LIVE, SHARED controller firmware
> (`src/esp32_controller_main.cpp` + `controller_*`), so it affects the production
> esp32dev controller as well as the S3 port. Get sign-off before implementing.

Grounded in a concurrency analysis of the current firmware (2026-06-16). Headline finding:
the controller's `app()` state is **already mutated concurrently without locking** — the
ESP-NOW receive callback (WiFi-stack task) calls `app().on_command_word/on_heartbeat/...`
directly, racing `loop()->tick()`. It survives only because the writes are word-atomic
scalars that self-correct each tick. There is **no reusable serialization mechanism** to
borrow, and MQTT's mutations are *worse* race candidates (an Arduino `String`
`g_disp_availability`, the device-registry array, and a multi-field shadow set).

This work is **three distinct pieces**, ordered by value/risk:

---

## Piece A — Weather wedge: PREVENT it (small, low-risk, high-value)  [was "Inc 4a"]

**Problem:** `ctrl_poll_weather` reboots (`esp_restart`) when the weather task genuinely
hangs. The hang is a **TLS handshake** that ignores the HTTP timeouts: `http.setTimeout`
and `http.setConnectTimeout` are set (4 s), but **`WiFiClientSecure::setHandshakeTimeout()`
is never called**, and there's no socket read timeout — so `mbedtls_ssl_handshake` can
block indefinitely.

**Fix (prevention, not recovery):** in `ctrl_fetch_zip_coordinates` and
`ctrl_fetch_pirateweather_current`, set `client.setHandshakeTimeout(kCtrlHttpTimeoutMs/1000)`
and a socket timeout before the request. The `WiFiClientSecure`/`HTTPClient` objects are
**stack-local**, so once every blocking call honors a timeout the function *returns*,
destructors run, no leak — the task can no longer wedge. The watchdog reboot becomes
unreachable (we keep it as a backstop but it should never fire).

**Why first:** ~10 lines, no concurrency, and (like the weather-wedge sign fix) it's shared
so production benefits immediately. Retires the genuine-hang reboot path.

**Risk:** low. Validate by confirming weather still fetches normally on the bench (needs a
valid PirateWeather key + zip configured), and that a forced-unreachable TLS host times out
and returns instead of hanging.

---

## Piece B — MQTT off the control loop via a command queue (large, the real Inc 3)

**Goal:** the control loop (core 1) must never block on MQTT connect (≤5 s), the 19-subscribe
+ discovery/state publish burst, or `PubSubClient.loop()`. Move all MQTT I/O to its own
FreeRTOS task; keep **all** `app()` mutation on the loop task.

**Approach — command queue (NOT a mutex):**
```
New MQTT task (core 0, alongside weather/web):
  owns g_ctrl_mqtt: ensure_connected / loop() / subscribe / publish.
  ctrl_mqtt_on_message: PARSE ONLY -> xQueueSend(g_ctrl_cmd_q, &CtrlCommand{...})

loop() task (core 1, unchanged role):
  at top of loop: drain g_ctrl_cmd_q (non-blocking) -> apply via the existing app()
    mutators + shadow/registry updates (single-threaded, same site as tick()).
  g_controller->tick(); g_relay_io.apply();   // unchanged — still the ONLY mutator site
```
- `CtrlCommand` = small POD tagged union (mode/fan/setpoint/lockout/windows/packed_word/
  heartbeat/indoor_temp/indoor_humidity/sync/reset_seq/...). The MQTT handler becomes a thin
  topic→command parser; the *application* logic stays where it is, just invoked from the
  drain step.
- Outbound publishes (state, discovery, announce) become "enqueue outbound" handled by the
  MQTT task — removes today's reentrant `publish()`-from-inside-its-own-callback.
- Queue depth ~16, with a dropped-command counter surfaced in telemetry (a full queue is a
  diagnosable event, never silent corruption).

**Why a queue beats a mutex (furnace-safety):** `tick()`/`compute_hvac_calls()` never see a
half-applied command (no relay glitch from torn state); FIFO preserves command ordering; the
`command_sources_` seq-dedup stays correct because it's touched from one task; and we avoid
taking a lock inside the ESP-NOW recv context (which could stall WiFi RX / invert priority
when loop holds it during an NVS write in `tick()`). `ControllerApp`/`Runtime` need **zero**
locking changes.

**Optional follow-up (not required):** route the ESP-NOW recv callback through the *same*
queue to finally retire the pre-existing ESP-NOW race. Strictly additive.

**Risk:** medium-high (it's the relay-control hot path). Mitigate with the queue design
(keeps mutation single-threaded), incremental rollout, and heavy review. Validate on bench:
commands (HA/MQTT) still apply correctly and promptly; broker outage no longer stalls the
control loop; relay decisions keep ticking during an MQTT publish burst.

---

## Piece C — Isolation reboot: recover-without-reboot (medium)  [was "Inc 4b"]

**Problem:** `loop()` calls `esp_restart()` when MQTT && ESP-NOW are both down >15 min.
**Fix:** fold this into the existing `NetworkRecoveryPolicy` escalation ladder. Add an
ESP-NOW re-init action (`esp_now_deinit()` + `EspNowControllerTransport::begin()` — re-reg
callbacks, re-add peers; `instance_`/`initialized_` already reset in `begin()`), and on
prolonged isolation escalate: MQTT reconnect → cycle ESP-NOW (+ WiFi reassoc on classic /
lwIP self-heal on Ethernet) → **only then** reboot as a true last resort. The
`RestartSubsystem` rung already exists; this wires ESP-NOW re-init into it and replaces the
blunt 15-min `esp_restart` with "restart subsystems once, reboot only if that fails."

**Risk:** medium (network re-init). Validate on bench: force prolonged isolation, confirm
ESP-NOW re-inits and recovers in place without a reboot; confirm a genuine unrecoverable
case still eventually reboots.

---

## Recommended sequence
1. **Piece A** (weather TLS handshake timeout) — quick, low-risk, ships to production now.
2. **Piece B** (MQTT command queue) — the headline; do it carefully with per-task review.
3. **Piece C** (isolation subsystem-restart) — builds on the recovery policy.

Each piece is independently shippable and independently testable on the bench (board on the
isolated test broker, ESP-NOW peer permitting for C).

## Open questions for review
1. **Scope/sequence:** do all three, or start with A (+ maybe C) and defer the larger B?
2. **MQTT task core:** core 0 (with weather/web, keeps control alone on core 1 —
   recommended) or core 1?
3. **Production rollout:** ship Piece A to prod (like the weather-wedge fix) once done, or
   batch all three behind the S3 port?
