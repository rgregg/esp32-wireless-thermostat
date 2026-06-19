# Ethernet-Primary Networking (Waveshare Controller) — Implementation Plan

> **Status: DRAFT — has open decisions that need Ryan's sign-off before execution.**
> Authored autonomously 2026-06-16 during the board bring-up session. The board
> hardware (W5500 Ethernet) is already validated (session log F6). This plan covers
> wiring Ethernet into the controller firmware as the primary IP link, with the WiFi
> radio demoted to ESP-NOW-only.

**Goal:** On the Waveshare board (`CONTROLLER_BOARD_WAVESHARE`), use wired W5500 Ethernet
for all IP traffic (MQTT, web UI, OTA, mDNS) and use the WiFi radio **only** for ESP-NOW
(STA mode, pinned channel, never associated to an AP). The classic `esp32dev` controller
is untouched — it keeps WiFi-primary. This kills the marginal-WiFi reboots that motivated
the whole port.

**Architecture (confirmed at the port-design stage):** Ethernet for IP + WiFi radio for
ESP-NOW only, no AP association, pinned ESP-NOW channel. Preserve HA device identity at
cutover (separate later step).

---

## Key insight — the clients are already link-agnostic

In Arduino-ESP32 3.x (IDF 5.x) the network stack is unified through lwIP. `WiFiClient` is
an alias of `NetworkClient`, whose TCP sockets route via the lwIP routing table over
whichever netif owns the route. `ETH.begin()` (W5500) registers an Ethernet netif. So:

- **MQTT** (`PubSubClient g_ctrl_mqtt(g_ctrl_wifi_client)`, main.cpp:67), the **WebServer**
  (port 80), **OTA**, and **mDNS** already work over Ethernet *unchanged* once an Ethernet
  netif has a route — we do NOT need to swap `WiFiClient` for an `EthernetClient`.

This shrinks the refactor dramatically. The real work is in the **gates and telemetry**
that hardcode `WiFi.status()`/`WiFi.localIP()`, plus the **WiFi radio role** and the
**watchdog**, not the data path.

## Seams that hardcode WiFi (from the architecture map)

| Seam | File:line | Change needed |
|------|-----------|---------------|
| MQTT connect gate | main.cpp:2210 `WiFi.status()!=WL_CONNECTED` | → `ctrl_ip_link_up()` |
| mDNS gate | main.cpp:2298 | → `ctrl_ip_link_up()` |
| Web server gate | main.cpp:1730 | → `ctrl_ip_link_up()` |
| Weather task gate | main.cpp:820 | → `ctrl_ip_link_up()` |
| MQTT announce IP | main.cpp:2292 `WiFi.localIP()` | → `ctrl_ip_local_addr()` |
| loop()/web heartbeat IP | main.cpp:2521 | → `ctrl_ip_local_addr()` |
| RSSI telemetry | main.cpp:1825–1827 | keep, but only when WiFi *associated* (Ethernet has no RSSI) |
| OTA rollback "healthy" | main.cpp:2597 | → `ctrl_ip_link_up() && mqtt.connected()` |
| WiFi watchdog | wifi_watchdog.h (whole) | rework — see Task 4 |
| ESP-NOW WiFi mode/channel | espnow_controller_transport.cpp:41,47 | pin channel; ensure no AP associate on Waveshare |

NTP/time: **none today** — no SNTP work required for this plan. (RTC plumbing is a
separate, later increment.)

---

## DECISIONS — RESOLVED (Ryan, 2026-06-16)

1. **WiFi STA fallback if Ethernet link drops? → NO.**
   WiFi never associates to an AP. If Ethernet drops, IP is down (no MQTT/web) but ESP-NOW
   keeps carrying control — that's the resilience story. No marginal-WiFi failure mode, no
   ESP-NOW-channel-vs-AP-channel conflict.

2. **What does the watchdog reboot on now? → ISOLATION ONLY.**
   The isolation watchdog (MQTT **and** ESP-NOW both down >15 min) is the **sole** reboot
   authority. The IP-link watchdog does NOT reboot on IP loss. On Waveshare it may still
   monitor the Ethernet link for telemetry/recovery, but never reboots.

3. **Keep WiFi provisioning (Improv BLE / web) on Waveshare? → DISABLE its start** (default,
   easily reversible). WiFi never associates, so the WiFi-creds provisioning flow is dead
   weight + prints a confusing "starting BLE provisioning" boot line. Gate the *start* off
   for `CONTROLLER_BOARD_WAVESHARE` (don't rip out the code). Device config (MQTT host,
   peers) is set via the web UI over Ethernet (DHCP + mDNS) or MQTT.

4. **Static IP / DHCP? → DHCP** (default). Recommend a **DHCP reservation** on the Ethernet
   MAC as an ops task (the Ethernet MAC is chip-derived and differs from the WiFi MAC).
   Static-IP-in-NVS can be added later if desired; not in this plan.

---

## Proposed task breakdown (incremental; each builds + is bench-validatable)

All changes gated to `#if defined(CONTROLLER_BOARD_WAVESHARE)` unless noted, so the classic
board is never affected. Validate each increment on the bench board over Ethernet via the
piserial5 flow.

### Task 0 — `lib_deps`/include for ETH; Ethernet bring-up in setup()
- Add `#include <ETH.h>` (Waveshare-gated). Call `ETH.begin(ETH_PHY_W5500, 1, 16, 12, -1,
  SPI2_HOST, 15, 14, 13)` early in setup() (pins validated, F6). Register an ETH event
  handler for `ARDUINO_EVENT_ETH_GOT_IP`/`ETH_DISCONNECTED` mirroring the WiFi one.
- Log link/IP on GOT_IP. Verify: boot → serial shows Ethernet IP (already proven in the
  standalone bench; here it's inside main's setup ordering).

### Task 1 — link-abstraction helpers (the core of the refactor)
- Add `bool ctrl_ip_link_up()` and `String ctrl_ip_local_addr()` (and maybe
  `const char* ctrl_ip_link_name()` → "eth"/"wifi"/"none"). On Waveshare: prefer
  `ETH.hasIP()`/`ETH.localIP()`; on classic: `WiFi.status()==WL_CONNECTED`/`WiFi.localIP()`.
  These are the only place that knows which link is primary.
- Unit-testable: extract the *decision* (given eth_up, eth_ip, wifi_up, wifi_ip → up?, addr)
  into a pure helper in `include/controller/` and native-test it. The Arduino glue stays thin.

### Task 2 — replace the WiFi gates with `ctrl_ip_link_up()` / `ctrl_ip_local_addr()`
- Swap the seams in the table above (web, mDNS, MQTT connect, weather, announce, heartbeats,
  OTA rollback). RSSI publish: only when WiFi is associated (skip on Ethernet-only).
- Verify: with WiFi creds absent, the Waveshare board brings up web + mDNS + MQTT **over
  Ethernet** (this is the headline validation — needs a reachable MQTT broker on the
  Ethernet LAN, see Validation).

### Task 3 — WiFi radio = ESP-NOW only on Waveshare
- On Waveshare: set `WiFi.mode(WIFI_STA)` but **do not** call `WiFi.begin()` to associate.
  Pin the ESP-NOW channel with `esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE)` (the
  transport already does this at espnow_controller_transport.cpp:47 — ensure nothing later
  re-associates and moves the channel). Skip `WifiProvisioningManager::begin()` /
  `ensure_connected()` on Waveshare (per Decision 3).
- Verify: ESP-NOW heartbeat + command path still works (display ↔ controller) with NO WiFi
  association, on the pinned channel, while Ethernet carries IP.

### Task 4 — watchdog rework (per Decisions 1 & 2)
- On Waveshare: replace the WiFi-STA watchdog with an Ethernet-link/gateway connectivity
  check for telemetry + (optional) link-recovery, **without** the IP-loss reboot. Keep the
  isolation watchdog (MQTT && ESP-NOW both down) as the only reboot authority.
- Verify: unplug Ethernet on the bench → no reboot; ESP-NOW keeps working; replug → IP
  services recover.

### Task 5 — telemetry/diagnostics
- Publish active link name + IP; add `eth_link`/`eth_ip` to state; gate `wifi_rssi` to
  WiFi-associated. Update the web UI status page strings.

---

## Validation plan
- Bench board on piserial5, Ethernet to the LAN (validated path). Needs a **reachable MQTT
  broker on the Ethernet subnet** for the end-to-end MQTT-over-Ethernet proof — confirm
  whether the bench Ethernet LAN can reach `mqtt.lan`, or stand up a throwaway broker.
- Per-increment: flash via the piserial5 esptool flow, read serial (reconnecting cat).
- Regression: classic `esp32-furnace-controller` must keep building + behaving (WiFi path
  unchanged) — it's fully gated out of these changes.

## Out of scope (separate plans)
- RTC plumbing into main (read RTC→settimeofday, log timestamps; SNTP writeback once IP is
  up). Modest; deferred — scheduling is a product decision (session log F5).
- Resilience increments 3–4 (MQTT off the main loop; recover-without-reboot for the weather
  + isolation esp_restart paths).
- Identity override + production cutover.
