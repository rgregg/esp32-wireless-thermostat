# ESP-NOW Fallback During WiFi Outages Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the controller and display working together over ESP-NOW when WiFi or the MQTT broker is down, with network-triggered reboots limited to a backed-off ladder.

**Architecture:** Two platform-agnostic, natively unit-tested policies make every decision: `EspNowChannelPolicy` (which channel the radio should be on, when to probe for the peer, when the display should leave WiFi) and `NetworkRebootGovernor` (in-place recovery, then backed-off reboots). A small Arduino-only template, `EspNowLinkManager`, executes the channel policy's actions against the radio and is shared by the controller and display firmware. The existing 15-minute isolation reboot and the WiFi watchdog's reboot are removed; the governor is the only network reboot authority.

**Tech Stack:** C++ (Arduino-ESP32 via pioarduino, ESP-IDF `esp_now` / `esp_wifi`), PlatformIO, the project's `TEST_CASE` / `ASSERT_*` native harness (`src/tests/test_harness.h`).

**Spec:** `docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md`

## Global Constraints

- Channels searched: only 1, 6 and 11, plus the last channel where the peer was heard. Never sweep 1-13.
- Rendezvous channel: the configured `espnow_channel` (default 6).
- Peer is "linked" when an authorized ESP-NOW frame was received within 30 s.
- Search pass interval: 20 s on the controller, 5 s on the display.
- Display leaves WiFi (`SuspendWifi`) after 60 s associated with MQTT down and peer not linked; retries WiFi every 5 minutes.
- WiFi rejoin interval: 5 s when the peer is not linked, 60 s when it is. `WiFi.setAutoReconnect(false)`.
- Reboot ladder (time isolated): recover in place at 2 min; reboot 1 at 10 min; reboot 2 at 1 h; reboot 3 at 4 h; then one per 24 h. Count stored in NVS key `net_rb_cnt`, written only when a reboot is taken or the count resets. Count resets after 10 continuous healthy minutes.
- Isolated = MQTT down AND peer not linked, using the transport's true ESP-NOW receive time, never `heartbeat_last_seen_ms`.
- The 5-minute furnace failsafe (`failsafe_timeout_ms = 300000`) is not changed.
- Non-network reboots (OTA, user request, weather wedge, task watchdog) are not changed.
- Shared logic lives in platform-agnostic headers in `include/` (no Arduino types), per the repo's `CLAUDE.md`.
- Do not flash production devices (`192.168.42.57`, `192.168.42.98`). Production rollout needs Ryan's explicit go-ahead after the bench list passes.
- Verification commands: `pio run -e native-tests && ./.pio/build/native-tests/program`, `pio run -e esp32-furnace-controller -e esp32-furnace-thermostat -e esp32-furnace-controller-waveshare-prod` (build multiple envs in ONE invocation, per `tasks/lessons.md`).

### Clarifications of the spec made in this plan

1. **Network-ready gate.** The spec keeps the controller's "no isolation before the network was ready once" gate. Applied literally, a device that rebooted during an outage would never count as isolated again, so reboots 2 and 3 would never happen. The governor therefore applies the gate only while the persisted reboot count is 0.
2. **WiFi watchdog.** The spec says the watchdog reports durations to the governor. The governor's isolation input (MQTT down and peer not linked) already covers both watchdog conditions, so the watchdog's reboot is simply removed and nothing is reported. Its forced reconnect stays, rate-limited to once per 2 minutes.
3. **Both devices park on the rendezvous channel between search passes** (the spec says only the controller). This is a strengthening that makes convergence role-independent.
4. **Link path naming.** The display already publishes `state/connection_path` with the value `disconnected`. The controller gets the same topic and the same four values (`mqtt`, `esp-now`, `mqtt+esp-now`, `disconnected`) instead of a new `none` value.
5. **Seconds until next reboot** is exposed on `/status` only. Over MQTT it would always read 0, because a device that can publish is not isolated.

## Review Focus

1. Peer associated to an AP on a channel outside 1/6/11: a device that last heard the peer there must try that channel first. (Task 2 test `channel_last_good_nonstandard_channel_is_tried_first`.)
2. Uptime past the 49-day `millis()` wrap: search, link timeout and the reboot ladder must keep working. (Task 2 test `channel_search_survives_millis_wraparound`; Task 3 test `governor_survives_millis_wraparound`.)
3. Cold power-on in the middle of an outage, with no network ever seen and no stored reboot count: must not reboot at all, and must still search for the peer. (Task 3 test `governor_cold_boot_in_outage_never_reboots`; Task 2 test `channel_search_runs_without_any_prior_link`.)
4. A flapping link (peer heard for a few seconds every few minutes): must not accumulate toward a reboot, and must not reset the stored count early. (Task 3 tests `governor_flapping_restarts_isolation_clock` and `governor_short_health_does_not_reset_count`.)
5. A probe whose send callback never arrives, or arrives late: the search must move on and must not park on the wrong channel. (Task 2 tests `channel_probe_timeout_moves_to_next_candidate` and `channel_late_ack_is_ignored`.)

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/bench/espnow_probe_spike.cpp` | Create | Bench-only sketch that measures whether a unicast send is acknowledged per channel |
| `include/transport/espnow_channel_policy.h` | Create | Channel policy types and class declaration |
| `src/transport/espnow_channel_policy.cpp` | Create | Channel policy logic |
| `src/tests/test_espnow_channel_policy.cpp` | Create | Native tests for the channel policy |
| `include/network_reboot_governor.h` | Create | Reboot governor types and class declaration |
| `src/network_reboot_governor.cpp` | Create | Reboot governor logic |
| `src/tests/test_network_reboot_governor.cpp` | Create | Native tests for the governor |
| `include/transport/espnow_fail_window.h` | Create | Header-only "any send failure in the last N ms" helper |
| `src/tests/test_espnow_fail_window.cpp` | Create | Native tests for the helper |
| `include/transport/espnow_link_manager.h` | Create | Arduino-only template that runs the channel policy against the radio |
| `include/controller/transport/espnow_controller_transport.h`, `src/controller/transport/espnow_controller_transport.cpp` | Modify | True peer receive time, probe send, unicast-peer query |
| `include/thermostat/transport/espnow_thermostat_transport.h`, `src/thermostat/transport/espnow_thermostat_transport.cpp` | Modify | Same, plus `restart()` |
| `include/thermostat/thermostat_device_runtime.h` | Modify | Expose the transport |
| `include/wifi_provisioning_manager.h`, `src/wifi_provisioning_manager.cpp` | Modify | Variable retry interval, suspend, no auto-reconnect |
| `include/wifi_watchdog.h` | Modify | Remove reboot path |
| `src/esp32_controller_main.cpp` | Modify | Wire link manager and governor, diagnostics |
| `src/thermostat/esp32s3_thermostat_firmware.cpp` | Modify | Wire link manager and governor, diagnostics |
| `include/discovery_topics.h` | Modify | Register new discovery entities |
| `platformio.ini` | Modify | `espnow-probe-spike` env |
| `docs/espnow-transport.md`, `AGENTS.md`, `docs/hil-checklist.md` | Modify | Document behaviour and bench scenarios |

---

### Task 1: Bench spike — is a unicast send acknowledged only on the peer's channel?

This task needs bench hardware and Ryan's hands (boards on `piserial5.lan`, see `docs/superpowers/notes/2026-06-16-esp32s3-bench-validation-notes.md`). Its output is an answer recorded in the spec. If the answer is "no", STOP after this task and report: Task 2's probe step must be redesigned to the spec's dwell fallback before continuing.

**Files:**
- Create: `src/bench/espnow_probe_spike.cpp`
- Modify: `platformio.ini` (append new env at end of file)
- Modify: `docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md` (record result under "Assumption to verify first")

**Interfaces:**
- Consumes: `transport/espnow_packets.h` (`HeartbeatPacket`, `PacketType::Heartbeat`, `kEspNowProtocolVersion`)
- Produces: a recorded measurement: per channel, send status (1 = acknowledged, 0 = not) and callback latency in microseconds. Task 2 relies on `probe_timeout_ms = 200` being at least twice the largest observed latency.

- [ ] **Step 1: Create the spike sketch**

`src/bench/espnow_probe_spike.cpp`:

```cpp
// Bench spike for the ESP-NOW fallback design (2026-10-13 spec, section 1).
// Question: does a unicast esp_now_send() report SUCCESS only on the channel the peer
// is listening on, and how long does the send callback take?
//
// The prober never joins WiFi. It cycles channels 1, 6, 11, sends one unicast heartbeat
// to SPIKE_PEER_MAC on each, and prints the send-callback status and latency.
// Build with the espnow-probe-spike env:
//   SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF pio run -e espnow-probe-spike
// Optional encryption: also set SPIKE_LMK to 32 hex chars.
#if defined(ESPNOW_PROBE_SPIKE)

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "transport/espnow_packets.h"

#ifndef SPIKE_PEER_MAC
#error "Set SPIKE_PEER_MAC, e.g. SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF"
#endif
#ifndef SPIKE_LMK
#define SPIKE_LMK ""
#endif

namespace {
std::atomic<int> g_status{-1};        // -1 pending, 0 fail, 1 success
std::atomic<uint32_t> g_cb_us{0};
uint8_t g_peer[6] = {0};

bool parse_mac(const char *s, uint8_t out[6]) {
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(v[i]);
  return true;
}

bool parse_lmk(const char *s, uint8_t out[16]) {
  if (strlen(s) != 32) return false;
  for (int i = 0; i < 16; ++i) {
    unsigned v;
    if (sscanf(s + i * 2, "%2x", &v) != 1) return false;
    out[i] = static_cast<uint8_t>(v);
  }
  return true;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);
  if (!parse_mac(SPIKE_PEER_MAC, g_peer)) {
    Serial.println("[spike] bad SPIKE_PEER_MAC");
    return;
  }
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[spike] esp_now_init failed");
    return;
  }
  esp_now_register_send_cb([](const esp_now_send_info_t *, esp_now_send_status_t status) {
    g_cb_us.store(micros());
    g_status.store(status == ESP_NOW_SEND_SUCCESS ? 1 : 0);
  });
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, g_peer, 6);
  peer.channel = 0;
  uint8_t lmk[16];
  peer.encrypt = parse_lmk(SPIKE_LMK, lmk);
  if (peer.encrypt) memcpy(peer.lmk, lmk, 16);
  const esp_err_t add_err = esp_now_add_peer(&peer);
  Serial.printf("[spike] my_mac=%s peer=%s encrypt=%d add_peer=%d\n",
                WiFi.macAddress().c_str(), SPIKE_PEER_MAC, peer.encrypt ? 1 : 0,
                static_cast<int>(add_err));
}

void loop() {
  static const uint8_t kChannels[] = {1, 6, 11};
  for (uint8_t ch : kChannels) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    thermostat::HeartbeatPacket pkt;
    pkt.header.type = static_cast<uint8_t>(thermostat::PacketType::Heartbeat);
    pkt.header.version = thermostat::kEspNowProtocolVersion;
    pkt.toggle = 0;
    g_status.store(-1);
    const uint32_t t0 = micros();
    const esp_err_t err =
        esp_now_send(g_peer, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
    while (g_status.load() == -1 && (micros() - t0) < 500000UL) {
      delay(1);
    }
    const int status = g_status.load();
    Serial.printf("[spike] ch=%u send_err=%d status=%d latency_us=%lu\n",
                  static_cast<unsigned>(ch), static_cast<int>(err), status,
                  status == -1 ? 0UL : static_cast<unsigned long>(g_cb_us.load() - t0));
    delay(100);
  }
  Serial.println("[spike] ---");
  delay(3000);
}

#endif  // ESPNOW_PROBE_SPIKE
```

- [ ] **Step 2: Add the build env**

Append to `platformio.ini`:

```ini
; --- ESP-NOW probe spike (2026-10-13 fallback design, Task 1) --------------------
; Bench-only. Cycles channels 1/6/11 sending a unicast heartbeat to SPIKE_PEER_MAC and
; prints whether each send was acknowledged and how long the callback took.
;   SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF [SPIKE_LMK=<32 hex>] pio run -e espnow-probe-spike
[env:espnow-probe-spike]
extends = env:esp32-furnace-thermostat-test
build_src_filter =
  -<*>
  +<bench/espnow_probe_spike.cpp>
build_flags =
  ${env:esp32-furnace-thermostat-test.build_flags}
  -DESPNOW_PROBE_SPIKE
  -DSPIKE_PEER_MAC=\"${sysenv.SPIKE_PEER_MAC}\"
  -DSPIKE_LMK=\"${sysenv.SPIKE_LMK}\"
```

- [ ] **Step 3: Build it and confirm other envs are unaffected**

Run: `SPIKE_PEER_MAC=AA:BB:CC:DD:EE:FF pio run -e espnow-probe-spike`
Expected: SUCCESS.

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: all existing tests pass (the spike file is fully inside `#if defined(ESPNOW_PROBE_SPIKE)`).

- [ ] **Step 4: Run the three bench cases (with Ryan)**

Flash the spike onto the spare display (`80B54ED1B804`) following the bench notes. For each case, capture at least 10 cycles of serial output.

| Case | Peer under test | Expected if the assumption holds |
|---|---|---|
| A | Bench controller in ESP-NOW-only mode (`controller-bench-espnow`), parked on its configured channel | `status=1` only on that channel; `status=0` on the other two |
| B | Bench controller joined to WiFi (`esp32-furnace-controller-test`); read its AP channel from UniFi | `status=1` only on the AP's channel |
| C | Case A or B repeated with `SPIKE_LMK` set to the bench LMK (`f0e1d2c3b4a596870718293a4b5c6d7e`) and the spike board's MAC added to the bench controller's device list via its `/config` page | Same channel selectivity as the unencrypted case |

In case C also note whether the controller's serial log `[espnow] rx=` counter increases, which settles the spec's open encryption question.

- [ ] **Step 5: Record the result in the spec**

Under "Assumption to verify first" in the spec, add a dated "Result" paragraph containing: pass/fail per case, the maximum observed `latency_us` for acknowledged and for failed sends, and the case C receive-counter observation.

Decision rule: the assumption holds if all three cases show `status=1` only on the peer's channel and the largest observed latency is under 100 ms. If it holds, continue to Task 2. If not, stop and report.

- [ ] **Step 6: Commit**

```bash
git add src/bench/espnow_probe_spike.cpp platformio.ini docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md
git commit -m "bench: add ESP-NOW probe spike and record acknowledgement results"
```

---

### Task 2: `EspNowChannelPolicy`

**Files:**
- Create: `include/transport/espnow_channel_policy.h`
- Create: `src/transport/espnow_channel_policy.cpp`
- Test: `src/tests/test_espnow_channel_policy.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `thermostat`):
  - `enum class LinkRole : uint8_t { Controller = 0, Display = 1 }`
  - `enum class ChannelActionType : uint8_t { None = 0, SetChannel = 1, Probe = 2, SuspendWifi = 3, ResumeWifi = 4 }`
  - `struct ChannelAction { ChannelActionType type; uint8_t channel; }`
  - `struct EspNowChannelConfig { LinkRole role; uint8_t rendezvous_channel; uint32_t link_timeout_ms, probe_timeout_ms, search_interval_ms, ack_hold_ms, mqtt_down_suspend_ms, wifi_resume_interval_ms, wifi_resume_window_ms; }`
  - `struct ChannelInputs { uint32_t now_ms; bool wifi_associated; uint8_t wifi_channel; bool mqtt_up; bool peer_heard; uint32_t last_peer_rx_ms; }`
  - `class EspNowChannelPolicy` with `ChannelAction poll(const ChannelInputs &)`, `void on_probe_result(bool acked)`, `bool linked(const ChannelInputs &) const`, `uint8_t current_channel() const`, `bool wifi_suspended() const`, `uint32_t search_count() const`

- [ ] **Step 1: Create the header**

`include/transport/espnow_channel_policy.h`:

```cpp
#pragma once

#include <stdint.h>

namespace thermostat {

enum class LinkRole : uint8_t { Controller = 0, Display = 1 };

enum class ChannelActionType : uint8_t {
  None = 0,
  SetChannel = 1,   // put the radio on ChannelAction::channel
  Probe = 2,        // send a unicast heartbeat to the peer, then report on_probe_result()
  SuspendWifi = 3,  // display only: leave WiFi so the radio is free to follow the peer
  ResumeWifi = 4,   // display only: try WiFi (and MQTT) again
};

struct ChannelAction {
  ChannelActionType type = ChannelActionType::None;
  uint8_t channel = 0;
};

struct EspNowChannelConfig {
  LinkRole role = LinkRole::Controller;
  uint8_t rendezvous_channel = 6;          // where both sides meet when neither has WiFi
  uint32_t link_timeout_ms = 30000;        // peer is "linked" if heard within this window
  uint32_t probe_timeout_ms = 200;         // wait this long for a probe acknowledgement
  uint32_t search_interval_ms = 20000;     // 20 s on the controller, 5 s on the display
  uint32_t ack_hold_ms = 30000;            // after an ack, stay put this long awaiting a frame
  uint32_t mqtt_down_suspend_ms = 60000;   // display: leave WiFi after MQTT down this long
  uint32_t wifi_resume_interval_ms = 300000;  // display: retry WiFi this often while suspended
  uint32_t wifi_resume_window_ms = 60000;  // display: time WiFi gets before suspending again
};

struct ChannelInputs {
  uint32_t now_ms = 0;
  bool wifi_associated = false;
  uint8_t wifi_channel = 0;      // valid only when wifi_associated
  bool mqtt_up = false;
  bool peer_heard = false;       // an authorized ESP-NOW frame has ever been received
  uint32_t last_peer_rx_ms = 0;  // when; valid only when peer_heard
};

// Decides which channel an ESP-NOW radio should be on so two devices can find each
// other when one or both have lost WiFi. Pure logic: no radio, WiFi or clock calls.
// The caller executes each returned action and reports probe outcomes.
//
// Only channels 1, 6 and 11 are searched, plus the last channel where the peer was
// heard. See docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md.
class EspNowChannelPolicy {
 public:
  explicit EspNowChannelPolicy(const EspNowChannelConfig &config = EspNowChannelConfig());

  ChannelAction poll(const ChannelInputs &in);
  // Report the outcome of the most recent Probe action. Ignored unless a probe is
  // still awaited.
  void on_probe_result(bool acked);

  bool linked(const ChannelInputs &in) const;
  uint8_t current_channel() const { return current_channel_; }
  bool wifi_suspended() const { return wifi_suspended_; }
  uint32_t search_count() const { return search_count_; }

 private:
  enum class State : uint8_t { Idle, NeedSet, NeedProbe, ProbeWait };
  enum class ProbeResult : uint8_t { Pending, Acked, Failed };

  void reset_search();
  void build_candidates();
  void add_candidate(uint8_t channel);

  EspNowChannelConfig config_{};
  State state_ = State::Idle;
  ProbeResult probe_result_ = ProbeResult::Pending;

  uint8_t current_channel_ = 6;
  uint8_t last_good_channel_ = 0;
  uint8_t candidates_[4] = {0, 0, 0, 0};
  uint8_t candidate_count_ = 0;
  uint8_t candidate_index_ = 0;

  bool pass_started_ = false;
  uint32_t last_pass_start_ms_ = 0;
  uint32_t probe_sent_ms_ = 0;
  bool holding_ = false;
  uint32_t hold_start_ms_ = 0;
  uint32_t search_count_ = 0;

  bool mqtt_down_ = false;
  uint32_t mqtt_down_since_ms_ = 0;
  bool wifi_suspended_ = false;
  uint32_t suspended_at_ms_ = 0;
  bool resumed_ = false;
  uint32_t resumed_at_ms_ = 0;
};

}  // namespace thermostat
```

- [ ] **Step 2: Write the failing tests**

`src/tests/test_espnow_channel_policy.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include <vector>

#include "test_harness.h"
#include "transport/espnow_channel_policy.h"

using thermostat::ChannelAction;
using thermostat::ChannelActionType;
using thermostat::ChannelInputs;
using thermostat::EspNowChannelConfig;
using thermostat::EspNowChannelPolicy;
using thermostat::LinkRole;

namespace {

EspNowChannelConfig cfg(LinkRole role, uint32_t interval_ms) {
  EspNowChannelConfig c;
  c.role = role;
  c.rendezvous_channel = 6;
  c.search_interval_ms = interval_ms;
  return c;
}

// Simulates a radio and an unassociated device searching for a peer that listens on
// `peer_channel` (0 = no peer anywhere). The peer never sends frames unless a test
// sets peer_heard/last_peer_rx_ms itself.
struct Sim {
  EspNowChannelPolicy policy;
  uint32_t now;
  uint8_t radio = 6;
  uint8_t peer_channel = 0;
  bool answer_probes = true;
  std::vector<uint8_t> probes;
  std::vector<uint8_t> sets;
  ChannelInputs in;

  Sim(const EspNowChannelConfig &c, uint32_t start_ms = 1000) : policy(c), now(start_ms) {}

  ChannelAction step(uint32_t dt_ms = 10) {
    now += dt_ms;
    in.now_ms = now;
    const ChannelAction a = policy.poll(in);
    if (a.type == ChannelActionType::SetChannel) {
      radio = a.channel;
      sets.push_back(a.channel);
    } else if (a.type == ChannelActionType::Probe) {
      probes.push_back(radio);
      if (answer_probes) policy.on_probe_result(peer_channel != 0 && radio == peer_channel);
    }
    return a;
  }

  void run(uint32_t duration_ms, uint32_t dt_ms = 10) {
    const uint32_t steps = duration_ms / dt_ms;
    for (uint32_t i = 0; i < steps; ++i) step(dt_ms);
  }
};

bool only_allowed(const std::vector<uint8_t> &chs, uint8_t extra) {
  for (uint8_t c : chs) {
    if (c != 1 && c != 6 && c != 11 && c != extra) return false;
  }
  return true;
}

}  // namespace

TEST_CASE(channel_associated_takes_no_channel_actions) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = true;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
  ASSERT_EQ(s.policy.search_count(), 0u);
}

TEST_CASE(channel_unassociated_linked_stays_parked) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.peer_heard = true;
  for (int i = 0; i < 1000; ++i) {
    s.in.last_peer_rx_ms = s.now;  // heard continuously
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
  ASSERT_EQ(s.policy.search_count(), 0u);
}

TEST_CASE(channel_search_order_is_last_good_then_rendezvous_then_rest) {
  Sim s(cfg(LinkRole::Controller, 20000));
  // Establish last-good channel 11 by being associated and linked there.
  s.in.wifi_associated = true;
  s.in.wifi_channel = 11;
  s.in.mqtt_up = true;
  s.in.peer_heard = true;
  s.in.last_peer_rx_ms = s.now;
  s.radio = 11;
  s.step();
  // Lose WiFi and the peer.
  s.in.wifi_associated = false;
  s.in.mqtt_up = false;
  s.run(31000);  // link times out, one pass runs
  ASSERT_TRUE(s.probes.size() >= 3);
  ASSERT_EQ(static_cast<int>(s.probes[0]), 11);
  ASSERT_EQ(static_cast<int>(s.probes[1]), 6);
  ASSERT_EQ(static_cast<int>(s.probes[2]), 1);
  // Between passes the radio is parked on the rendezvous channel.
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 6);
}

TEST_CASE(channel_search_only_uses_1_6_11) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.run(300000, 50);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_TRUE(only_allowed(s.probes, 0));
  ASSERT_TRUE(only_allowed(s.sets, 0));
}

TEST_CASE(channel_last_good_nonstandard_channel_is_tried_first) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 3;
  s.in.mqtt_up = true;
  s.in.peer_heard = true;
  s.in.last_peer_rx_ms = s.now;
  s.radio = 3;
  s.step();
  s.in.wifi_associated = false;
  s.in.mqtt_up = false;
  s.peer_channel = 3;
  s.run(31000);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_EQ(static_cast<int>(s.probes[0]), 3);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 3);
  ASSERT_TRUE(only_allowed(s.probes, 3));
}

TEST_CASE(channel_search_runs_without_any_prior_link) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 11;
  s.run(2000);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 11);
  ASSERT_EQ(s.policy.search_count(), 1u);
}

TEST_CASE(channel_ack_holds_channel_until_hold_expires) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 1;
  s.run(2000);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
  const size_t sets_after_find = s.sets.size();
  const size_t probes_after_find = s.probes.size();
  s.run(25000);  // inside the 30 s hold, peer still silent
  ASSERT_EQ(s.sets.size(), sets_after_find);
  ASSERT_EQ(s.probes.size(), probes_after_find);
  s.run(10000);  // hold expired with no frame: searching resumes
  ASSERT_TRUE(s.probes.size() > probes_after_find);
}

TEST_CASE(channel_link_established_after_ack_stops_search) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.peer_channel = 1;
  s.run(2000);
  s.in.peer_heard = true;
  const uint32_t count = s.policy.search_count();
  for (int i = 0; i < 6000; ++i) {
    s.in.last_peer_rx_ms = s.now;
    s.step();
  }
  ASSERT_EQ(s.policy.search_count(), count);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 1);
}

TEST_CASE(channel_search_interval_differs_by_config) {
  Sim controller(cfg(LinkRole::Controller, 20000));
  controller.run(59000);
  ASSERT_EQ(controller.policy.search_count(), 3u);  // t=0, 20 s, 40 s

  Sim display(cfg(LinkRole::Display, 5000));
  display.run(59000);
  ASSERT_EQ(display.policy.search_count(), 12u);  // every 5 s
}

TEST_CASE(channel_probe_timeout_moves_to_next_candidate) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;  // send callback never arrives
  s.run(2000);
  ASSERT_TRUE(s.probes.size() >= 3);
  ASSERT_EQ(static_cast<int>(s.probes[0]), 6);
  ASSERT_EQ(static_cast<int>(s.probes[1]), 1);
  ASSERT_EQ(static_cast<int>(s.probes[2]), 11);
}

TEST_CASE(channel_late_ack_is_ignored) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.answer_probes = false;
  s.run(450);  // probe on 6 timed out; policy has moved on to channel 1
  ASSERT_TRUE(s.probes.size() >= 2);
  const uint8_t before = s.policy.current_channel();
  s.run(1000);  // pass finishes
  s.policy.on_probe_result(true);  // stale ack, no probe awaited
  s.run(100);
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 6);  // parked on rendezvous
  (void)before;
  // The stale ack must not have started a hold: the next pass still runs on schedule.
  const uint32_t count = s.policy.search_count();
  s.run(6000);
  ASSERT_TRUE(s.policy.search_count() > count);
}

TEST_CASE(channel_search_survives_millis_wraparound) {
  Sim s(cfg(LinkRole::Display, 5000), 0xFFFFF000u);
  s.peer_channel = 11;
  s.run(20000);  // crosses the wrap
  ASSERT_EQ(static_cast<int>(s.policy.current_channel()), 11);
}

TEST_CASE(channel_invalid_rendezvous_falls_back_to_6) {
  EspNowChannelConfig c = cfg(LinkRole::Controller, 20000);
  c.rendezvous_channel = 0;
  Sim s(c);
  s.run(2000);
  ASSERT_TRUE(!s.probes.empty());
  ASSERT_EQ(static_cast<int>(s.probes[0]), 6);
}

TEST_CASE(channel_display_suspends_wifi_when_broker_down_and_peer_unlinked) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  bool suspended = false;
  uint32_t suspended_after = 0;
  const uint32_t start = s.now;
  for (int i = 0; i < 7000 && !suspended; ++i) {
    if (s.step().type == ChannelActionType::SuspendWifi) {
      suspended = true;
      suspended_after = s.now - start;
    }
  }
  ASSERT_TRUE(suspended);
  ASSERT_TRUE(suspended_after >= 60000u && suspended_after < 61000u);
  ASSERT_TRUE(s.policy.wifi_suspended());
}

TEST_CASE(channel_controller_never_suspends_wifi) {
  Sim s(cfg(LinkRole::Controller, 20000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  for (int i = 0; i < 20000; ++i) {
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

TEST_CASE(channel_display_stays_on_wifi_when_peer_linked) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  s.in.peer_heard = true;
  for (int i = 0; i < 20000; ++i) {
    s.in.last_peer_rx_ms = s.now;
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

TEST_CASE(channel_display_resumes_wifi_then_waits_before_suspending_again) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  while (s.step().type != ChannelActionType::SuspendWifi) {
  }
  const uint32_t suspended_at = s.now;
  s.in.wifi_associated = false;  // caller disconnected
  bool resumed = false;
  while (!resumed) {
    resumed = s.step().type == ChannelActionType::ResumeWifi;
  }
  ASSERT_TRUE(s.now - suspended_at >= 300000u && s.now - suspended_at < 301000u);
  ASSERT_TRUE(!s.policy.wifi_suspended());
  // WiFi comes back, broker still down: no new suspend inside the 60 s window.
  s.in.wifi_associated = true;
  const uint32_t resumed_at = s.now;
  bool suspended_again = false;
  while (!suspended_again) {
    suspended_again = s.step().type == ChannelActionType::SuspendWifi;
  }
  ASSERT_TRUE(s.now - resumed_at >= 60000u && s.now - resumed_at < 61000u);
}

TEST_CASE(channel_display_does_not_suspend_when_mqtt_returns) {
  Sim s(cfg(LinkRole::Display, 5000));
  s.in.wifi_associated = true;
  s.in.wifi_channel = 1;
  s.in.mqtt_up = false;
  s.run(50000);
  s.in.mqtt_up = true;
  s.run(5000);
  s.in.mqtt_up = false;  // down again: the 60 s clock restarts
  for (int i = 0; i < 5000; ++i) {  // 50 s
    ASSERT_TRUE(s.step().type == ChannelActionType::None);
  }
}

#endif
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio run -e native-tests`
Expected: FAIL at link time with undefined references to `thermostat::EspNowChannelPolicy::poll` and the other methods.

- [ ] **Step 4: Write the implementation**

`src/transport/espnow_channel_policy.cpp`:

```cpp
#include "transport/espnow_channel_policy.h"

namespace thermostat {

namespace {
constexpr uint8_t kSearchChannels[] = {1, 6, 11};
constexpr uint8_t kDefaultRendezvous = 6;

bool valid_channel(uint8_t c) { return c >= 1 && c <= 14; }
}  // namespace

EspNowChannelPolicy::EspNowChannelPolicy(const EspNowChannelConfig &config)
    : config_(config) {
  if (!valid_channel(config_.rendezvous_channel)) {
    config_.rendezvous_channel = kDefaultRendezvous;
  }
  current_channel_ = config_.rendezvous_channel;
}

bool EspNowChannelPolicy::linked(const ChannelInputs &in) const {
  return in.peer_heard &&
         static_cast<uint32_t>(in.now_ms - in.last_peer_rx_ms) < config_.link_timeout_ms;
}

void EspNowChannelPolicy::reset_search() {
  state_ = State::Idle;
  holding_ = false;
  pass_started_ = false;
}

void EspNowChannelPolicy::add_candidate(uint8_t channel) {
  if (!valid_channel(channel)) return;
  for (uint8_t i = 0; i < candidate_count_; ++i) {
    if (candidates_[i] == channel) return;
  }
  if (candidate_count_ < sizeof(candidates_)) {
    candidates_[candidate_count_++] = channel;
  }
}

void EspNowChannelPolicy::build_candidates() {
  candidate_count_ = 0;
  candidate_index_ = 0;
  add_candidate(last_good_channel_);
  add_candidate(config_.rendezvous_channel);
  for (uint8_t c : kSearchChannels) add_candidate(c);
}

void EspNowChannelPolicy::on_probe_result(bool acked) {
  if (state_ == State::ProbeWait && probe_result_ == ProbeResult::Pending) {
    probe_result_ = acked ? ProbeResult::Acked : ProbeResult::Failed;
  }
}

ChannelAction EspNowChannelPolicy::poll(const ChannelInputs &in) {
  const uint32_t now = in.now_ms;
  const bool is_linked = linked(in);

  if (in.mqtt_up) {
    mqtt_down_ = false;
  } else if (!mqtt_down_) {
    mqtt_down_ = true;
    mqtt_down_since_ms_ = now;
  }

  if (wifi_suspended_ &&
      static_cast<uint32_t>(now - suspended_at_ms_) >= config_.wifi_resume_interval_ms) {
    wifi_suspended_ = false;
    resumed_ = true;
    resumed_at_ms_ = now;
    reset_search();
    return {ChannelActionType::ResumeWifi, 0};
  }

  if (in.wifi_associated) {
    if (wifi_suspended_) {
      // The caller has not finished disconnecting yet; the AP still owns the channel.
      return {ChannelActionType::None, 0};
    }
    reset_search();
    if (valid_channel(in.wifi_channel)) {
      current_channel_ = in.wifi_channel;
      if (is_linked) last_good_channel_ = in.wifi_channel;
    }
    const bool in_resume_window =
        resumed_ &&
        static_cast<uint32_t>(now - resumed_at_ms_) < config_.wifi_resume_window_ms;
    if (config_.role == LinkRole::Display && mqtt_down_ && !is_linked && !in_resume_window &&
        static_cast<uint32_t>(now - mqtt_down_since_ms_) >= config_.mqtt_down_suspend_ms) {
      wifi_suspended_ = true;
      suspended_at_ms_ = now;
      return {ChannelActionType::SuspendWifi, 0};
    }
    return {ChannelActionType::None, 0};
  }

  // Not associated: the radio is free to move.
  if (is_linked) {
    reset_search();
    last_good_channel_ = current_channel_;
    return {ChannelActionType::None, 0};
  }

  if (state_ == State::Idle) {
    if (holding_) {
      if (static_cast<uint32_t>(now - hold_start_ms_) < config_.ack_hold_ms) {
        return {ChannelActionType::None, 0};
      }
      holding_ = false;
    }
    const bool pass_due =
        !pass_started_ ||
        static_cast<uint32_t>(now - last_pass_start_ms_) >= config_.search_interval_ms;
    if (!pass_due) {
      if (current_channel_ != config_.rendezvous_channel) {
        current_channel_ = config_.rendezvous_channel;
        return {ChannelActionType::SetChannel, current_channel_};
      }
      return {ChannelActionType::None, 0};
    }
    build_candidates();
    pass_started_ = true;
    last_pass_start_ms_ = now;
    ++search_count_;
    state_ = State::NeedSet;
  }

  if (state_ == State::NeedSet) {
    const uint8_t channel = candidates_[candidate_index_];
    state_ = State::NeedProbe;
    if (channel != current_channel_) {
      current_channel_ = channel;
      return {ChannelActionType::SetChannel, channel};
    }
  }

  if (state_ == State::NeedProbe) {
    state_ = State::ProbeWait;
    probe_result_ = ProbeResult::Pending;
    probe_sent_ms_ = now;
    return {ChannelActionType::Probe, current_channel_};
  }

  // State::ProbeWait
  if (probe_result_ == ProbeResult::Pending &&
      static_cast<uint32_t>(now - probe_sent_ms_) < config_.probe_timeout_ms) {
    return {ChannelActionType::None, 0};
  }
  if (probe_result_ == ProbeResult::Acked) {
    last_good_channel_ = current_channel_;
    holding_ = true;
    hold_start_ms_ = now;
    state_ = State::Idle;
    return {ChannelActionType::None, 0};
  }
  ++candidate_index_;
  state_ = (candidate_index_ < candidate_count_) ? State::NeedSet : State::Idle;
  return {ChannelActionType::None, 0};
}

}  // namespace thermostat
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: every `channel_*` test prints `[PASS]`, and the summary line shows `failures: 0`.

If `channel_search_interval_differs_by_config` or `channel_late_ack_is_ignored` fails on an off-by-one in timing, fix the policy, not the test: the constants in the test come from Global Constraints.

- [ ] **Step 6: Commit**

```bash
git add include/transport/espnow_channel_policy.h src/transport/espnow_channel_policy.cpp src/tests/test_espnow_channel_policy.cpp
git commit -m "feat(transport): add EspNowChannelPolicy for peer-following channel agreement"
```

---

### Task 3: `NetworkRebootGovernor`

**Files:**
- Create: `include/network_reboot_governor.h`
- Create: `src/network_reboot_governor.cpp`
- Test: `src/tests/test_network_reboot_governor.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `thermostat`):
  - `enum class GovernorAction : uint8_t { None = 0, RecoverInPlace = 1, Reboot = 2 }`
  - `struct NetworkRebootConfig { uint32_t recover_after_ms, first_reboot_after_ms, second_reboot_after_ms, third_reboot_after_ms, later_reboot_after_ms, healthy_reset_ms; }`
  - `class NetworkRebootGovernor` with constructor `(const NetworkRebootConfig & = NetworkRebootConfig(), uint32_t persisted_reboot_count = 0)`, `GovernorAction poll(uint32_t now_ms, bool isolated, bool network_was_ready)`, `uint32_t reboot_count() const`, `bool take_count_reset()`, `uint32_t ms_until_reboot(uint32_t now_ms) const`
  - Contract: when `poll` returns `Reboot`, `reboot_count()` has already been incremented; the caller persists it and restarts. When `take_count_reset()` returns true, the caller persists 0.

- [ ] **Step 1: Create the header**

`include/network_reboot_governor.h`:

```cpp
#pragma once

#include <stdint.h>

namespace thermostat {

enum class GovernorAction : uint8_t {
  None = 0,
  RecoverInPlace = 1,  // re-initialise ESP-NOW / WiFi / MQTT without rebooting
  Reboot = 2,          // persist reboot_count(), then restart
};

struct NetworkRebootConfig {
  uint32_t recover_after_ms = 2UL * 60UL * 1000UL;
  uint32_t first_reboot_after_ms = 10UL * 60UL * 1000UL;
  uint32_t second_reboot_after_ms = 60UL * 60UL * 1000UL;
  uint32_t third_reboot_after_ms = 4UL * 60UL * 60UL * 1000UL;
  uint32_t later_reboot_after_ms = 24UL * 60UL * 60UL * 1000UL;
  uint32_t healthy_reset_ms = 10UL * 60UL * 1000UL;
};

// Sole authority for network-triggered reboots. A device is "isolated" when it can
// reach neither MQTT nor its ESP-NOW peer. Isolation leads to one in-place recovery,
// then reboots spaced further and further apart so a long outage cannot cause a reboot
// loop. The consecutive-reboot count is supplied at construction (from NVS) and must be
// persisted by the caller whenever it changes. Pure logic: no clock or NVS calls.
class NetworkRebootGovernor {
 public:
  explicit NetworkRebootGovernor(const NetworkRebootConfig &config = NetworkRebootConfig(),
                                 uint32_t persisted_reboot_count = 0)
      : config_(config), reboot_count_(persisted_reboot_count) {}

  // isolated: MQTT down AND ESP-NOW peer not linked, right now.
  // network_was_ready: the IP network has been usable at least once this boot. While
  // the stored reboot count is 0, isolation is not counted until this is true, so a
  // slow first DHCP/DNS bring-up can never cause a reboot.
  GovernorAction poll(uint32_t now_ms, bool isolated, bool network_was_ready);

  uint32_t reboot_count() const { return reboot_count_; }
  // True exactly once after the count was reset to 0 by sustained health.
  bool take_count_reset();
  // Milliseconds until the next reboot would be taken; 0 when isolation is not counting.
  uint32_t ms_until_reboot(uint32_t now_ms) const;

 private:
  uint32_t reboot_threshold_ms() const;

  NetworkRebootConfig config_{};
  uint32_t reboot_count_ = 0;
  bool count_reset_pending_ = false;

  bool isolating_ = false;
  uint32_t isolation_start_ms_ = 0;
  bool recovered_ = false;
  bool reboot_issued_ = false;

  bool healthy_ = false;
  uint32_t healthy_since_ms_ = 0;
};

}  // namespace thermostat
```

- [ ] **Step 2: Write the failing tests**

`src/tests/test_network_reboot_governor.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "network_reboot_governor.h"
#include "test_harness.h"

using thermostat::GovernorAction;
using thermostat::NetworkRebootConfig;
using thermostat::NetworkRebootGovernor;

namespace {
constexpr uint32_t kMin = 60UL * 1000UL;
constexpr uint32_t kHour = 60UL * kMin;

struct Outcome {
  uint32_t recover_at = 0;
  uint32_t reboot_at = 0;
  int recovers = 0;
  int reboots = 0;
};

// Polls every second while isolated, from `start` for `duration`.
Outcome run_isolated(NetworkRebootGovernor &g, uint32_t start, uint32_t duration,
                     bool network_was_ready = true) {
  Outcome o;
  for (uint32_t t = 0; t <= duration; t += 1000) {
    const GovernorAction a = g.poll(start + t, true, network_was_ready);
    if (a == GovernorAction::RecoverInPlace) {
      ++o.recovers;
      if (o.recover_at == 0) o.recover_at = t;
    } else if (a == GovernorAction::Reboot) {
      ++o.reboots;
      if (o.reboot_at == 0) o.reboot_at = t;
    }
  }
  return o;
}
}  // namespace

TEST_CASE(governor_healthy_takes_no_action) {
  NetworkRebootGovernor g;
  for (uint32_t t = 0; t < 2 * kHour; t += 1000) {
    ASSERT_TRUE(g.poll(t, false, true) == GovernorAction::None);
  }
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_TRUE(!g.take_count_reset());
}

TEST_CASE(governor_first_episode_recovers_at_2min_reboots_at_10min) {
  NetworkRebootGovernor g;
  const Outcome o = run_isolated(g, 5000, 30 * kMin);
  ASSERT_EQ(o.recovers, 1);
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboots, 1);  // returned once even if the caller fails to restart
  ASSERT_EQ(o.reboot_at, 10 * kMin);
  ASSERT_EQ(g.reboot_count(), 1u);
}

TEST_CASE(governor_ladder_follows_persisted_count) {
  NetworkRebootGovernor g1(NetworkRebootConfig(), 1);
  const Outcome o1 = run_isolated(g1, 0, 2 * kHour);
  ASSERT_EQ(o1.reboot_at, 1 * kHour);
  ASSERT_EQ(g1.reboot_count(), 2u);

  NetworkRebootGovernor g2(NetworkRebootConfig(), 2);
  const Outcome o2 = run_isolated(g2, 0, 5 * kHour);
  ASSERT_EQ(o2.reboot_at, 4 * kHour);
  ASSERT_EQ(g2.reboot_count(), 3u);

  NetworkRebootGovernor g3(NetworkRebootConfig(), 3);
  const Outcome o3 = run_isolated(g3, 0, 25 * kHour);
  ASSERT_EQ(o3.reboot_at, 24 * kHour);
  ASSERT_EQ(g3.reboot_count(), 4u);

  NetworkRebootGovernor g9(NetworkRebootConfig(), 9);
  const Outcome o9 = run_isolated(g9, 0, 25 * kHour);
  ASSERT_EQ(o9.reboot_at, 24 * kHour);
}

TEST_CASE(governor_total_outage_has_three_reboots_in_first_24h) {
  // Simulate the device restarting each time Reboot is returned.
  uint32_t persisted = 0;
  uint32_t elapsed = 0;
  int reboots = 0;
  while (elapsed < 24 * kHour) {
    NetworkRebootGovernor g(NetworkRebootConfig(), persisted);
    const Outcome o = run_isolated(g, 0, 24 * kHour - elapsed);
    if (o.reboots == 0) break;
    ++reboots;
    elapsed += o.reboot_at;
    persisted = g.reboot_count();
  }
  ASSERT_EQ(reboots, 3);  // at 10 min, 1 h 10 min, 5 h 10 min
}

TEST_CASE(governor_recovers_once_per_episode) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 3);  // 24 h to reboot: long episode
  const Outcome o = run_isolated(g, 0, 6 * kHour);
  ASSERT_EQ(o.recovers, 1);
  // A new episode gets its own recovery.
  ASSERT_TRUE(g.poll(6 * kHour + 1000, false, true) == GovernorAction::None);
  const Outcome o2 = run_isolated(g, 6 * kHour + 2000, 5 * kMin);
  ASSERT_EQ(o2.recovers, 1);
}

TEST_CASE(governor_cold_boot_in_outage_never_reboots) {
  NetworkRebootGovernor g;  // stored count 0, network never ready
  const Outcome o = run_isolated(g, 0, 48 * kHour, false);
  ASSERT_EQ(o.recovers, 0);
  ASSERT_EQ(o.reboots, 0);
  ASSERT_EQ(g.ms_until_reboot(48 * kHour), 0u);
}

TEST_CASE(governor_rebooted_into_outage_keeps_laddering_without_network) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 1);  // we rebooted for isolation once
  const Outcome o = run_isolated(g, 0, 2 * kHour, false);
  ASSERT_EQ(o.reboots, 1);
  ASSERT_EQ(o.reboot_at, 1 * kHour);
}

TEST_CASE(governor_flapping_restarts_isolation_clock) {
  NetworkRebootGovernor g;
  uint32_t t = 0;
  int reboots = 0;
  // 9 minutes isolated, 5 seconds healthy, repeated for 3 hours: never reaches 10 min.
  for (int cycle = 0; cycle < 20; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) {
      if (g.poll(t, true, true) == GovernorAction::Reboot) ++reboots;
    }
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) {
      g.poll(t, false, true);
    }
  }
  ASSERT_EQ(reboots, 0);
}

TEST_CASE(governor_count_resets_after_10_healthy_minutes) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  for (uint32_t t = 0; t < 10 * kMin; t += 1000) {
    g.poll(t, false, true);
    ASSERT_TRUE(!g.take_count_reset());
  }
  g.poll(10 * kMin, false, true);
  ASSERT_EQ(g.reboot_count(), 0u);
  ASSERT_TRUE(g.take_count_reset());
  ASSERT_TRUE(!g.take_count_reset());  // reported once
}

TEST_CASE(governor_short_health_does_not_reset_count) {
  NetworkRebootGovernor g(NetworkRebootConfig(), 2);
  uint32_t t = 0;
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (uint32_t i = 0; i < 9 * kMin; i += 1000, t += 1000) g.poll(t, false, true);
    for (uint32_t i = 0; i < 5000; i += 1000, t += 1000) g.poll(t, true, true);
  }
  ASSERT_EQ(g.reboot_count(), 2u);
  ASSERT_TRUE(!g.take_count_reset());
}

TEST_CASE(governor_ms_until_reboot_counts_down) {
  NetworkRebootGovernor g;
  ASSERT_EQ(g.ms_until_reboot(0), 0u);  // not isolated
  g.poll(1000, true, true);
  ASSERT_EQ(g.ms_until_reboot(1000), 10 * kMin);
  g.poll(4 * kMin + 1000, true, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 1000), 6 * kMin);
  g.poll(4 * kMin + 2000, false, true);
  ASSERT_EQ(g.ms_until_reboot(4 * kMin + 2000), 0u);
}

TEST_CASE(governor_survives_millis_wraparound) {
  NetworkRebootGovernor g;
  const Outcome o = run_isolated(g, 0xFFFFF000u, 30 * kMin);  // wraps after ~4 s
  ASSERT_EQ(o.recover_at, 2 * kMin);
  ASSERT_EQ(o.reboot_at, 10 * kMin);
}

#endif
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio run -e native-tests`
Expected: FAIL at link time with undefined references to `thermostat::NetworkRebootGovernor::poll`.

- [ ] **Step 4: Write the implementation**

`src/network_reboot_governor.cpp`:

```cpp
#include "network_reboot_governor.h"

namespace thermostat {

uint32_t NetworkRebootGovernor::reboot_threshold_ms() const {
  switch (reboot_count_) {
    case 0:
      return config_.first_reboot_after_ms;
    case 1:
      return config_.second_reboot_after_ms;
    case 2:
      return config_.third_reboot_after_ms;
    default:
      return config_.later_reboot_after_ms;
  }
}

bool NetworkRebootGovernor::take_count_reset() {
  const bool pending = count_reset_pending_;
  count_reset_pending_ = false;
  return pending;
}

uint32_t NetworkRebootGovernor::ms_until_reboot(uint32_t now_ms) const {
  if (!isolating_ || reboot_issued_) return 0;
  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);
  const uint32_t threshold = reboot_threshold_ms();
  return elapsed >= threshold ? 0 : threshold - elapsed;
}

GovernorAction NetworkRebootGovernor::poll(uint32_t now_ms, bool isolated,
                                           bool network_was_ready) {
  if (!isolated) {
    isolating_ = false;
    recovered_ = false;
    reboot_issued_ = false;
    if (!healthy_) {
      healthy_ = true;
      healthy_since_ms_ = now_ms;
    } else if (reboot_count_ > 0 &&
               static_cast<uint32_t>(now_ms - healthy_since_ms_) >= config_.healthy_reset_ms) {
      reboot_count_ = 0;
      count_reset_pending_ = true;
    }
    return GovernorAction::None;
  }

  healthy_ = false;

  // Do not count isolation on a boot that has never had a usable network, unless we
  // already know we are in an outage (a previous isolation reboot was recorded).
  if (!network_was_ready && reboot_count_ == 0) {
    isolating_ = false;
    return GovernorAction::None;
  }

  if (!isolating_) {
    isolating_ = true;
    isolation_start_ms_ = now_ms;
    recovered_ = false;
    reboot_issued_ = false;
  }

  const uint32_t elapsed = static_cast<uint32_t>(now_ms - isolation_start_ms_);

  if (!reboot_issued_ && elapsed >= reboot_threshold_ms()) {
    reboot_issued_ = true;
    ++reboot_count_;
    return GovernorAction::Reboot;
  }
  if (!recovered_ && elapsed >= config_.recover_after_ms) {
    recovered_ = true;
    return GovernorAction::RecoverInPlace;
  }
  return GovernorAction::None;
}

}  // namespace thermostat
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: every `governor_*` test prints `[PASS]`; summary shows `failures: 0`.

- [ ] **Step 6: Commit**

```bash
git add include/network_reboot_governor.h src/network_reboot_governor.cpp src/tests/test_network_reboot_governor.cpp
git commit -m "feat: add NetworkRebootGovernor with backed-off isolation reboots"
```

---

### Task 4: `RecentFailWindow`

**Files:**
- Create: `include/transport/espnow_fail_window.h`
- Test: `src/tests/test_espnow_fail_window.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `thermostat::RecentFailWindow` (header-only) with constructor `(uint32_t window_ms = 300000)`, `void update(uint32_t now_ms, uint32_t total_fail_count)`, `bool recent(uint32_t now_ms) const`.

- [ ] **Step 1: Write the failing test**

`src/tests/test_espnow_fail_window.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "test_harness.h"
#include "transport/espnow_fail_window.h"

using thermostat::RecentFailWindow;

TEST_CASE(fail_window_none_when_counter_never_moves) {
  RecentFailWindow w;
  for (uint32_t t = 0; t < 1000000; t += 5000) {
    w.update(t, 0);
    ASSERT_TRUE(!w.recent(t));
  }
}

TEST_CASE(fail_window_reports_failure_for_5_minutes_then_clears) {
  RecentFailWindow w;
  w.update(1000, 0);
  w.update(6000, 1);  // one failure
  ASSERT_TRUE(w.recent(6000));
  w.update(300000, 1);
  ASSERT_TRUE(w.recent(300000));    // 294 s later: still inside the window
  w.update(306000, 1);
  ASSERT_TRUE(!w.recent(306000));   // 300 s after the failure: cleared
}

TEST_CASE(fail_window_new_failure_extends_window) {
  RecentFailWindow w;
  w.update(1000, 1);
  w.update(200000, 2);
  ASSERT_TRUE(w.recent(450000));    // 250 s after the second failure
  ASSERT_TRUE(!w.recent(500001));
}

TEST_CASE(fail_window_survives_millis_wraparound) {
  RecentFailWindow w;
  w.update(0xFFFFFF00u, 1);
  ASSERT_TRUE(w.recent(0x00000100u));  // 512 ms later, across the wrap
}

#endif
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio run -e native-tests`
Expected: FAIL to compile with `transport/espnow_fail_window.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`include/transport/espnow_fail_window.h`:

```cpp
#pragma once

#include <stdint.h>

namespace thermostat {

// Turns a monotonically increasing failure counter into "has there been a failure in
// the last window_ms?". Used so the ESP-NOW error diagnostic clears itself instead of
// latching on the first failed send since boot.
class RecentFailWindow {
 public:
  explicit RecentFailWindow(uint32_t window_ms = 300000) : window_ms_(window_ms) {}

  void update(uint32_t now_ms, uint32_t total_fail_count) {
    if (total_fail_count != last_total_) {
      last_total_ = total_fail_count;
      has_fail_ = true;
      last_fail_ms_ = now_ms;
    }
  }

  bool recent(uint32_t now_ms) const {
    return has_fail_ && static_cast<uint32_t>(now_ms - last_fail_ms_) < window_ms_;
  }

 private:
  uint32_t window_ms_;
  uint32_t last_total_ = 0;
  bool has_fail_ = false;
  uint32_t last_fail_ms_ = 0;
};

}  // namespace thermostat
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: every `fail_window_*` test prints `[PASS]`; summary shows `failures: 0`.

- [ ] **Step 5: Commit**

```bash
git add include/transport/espnow_fail_window.h src/tests/test_espnow_fail_window.cpp
git commit -m "feat(transport): add RecentFailWindow for self-clearing ESP-NOW error state"
```

---

### Task 5: Radio plumbing shared by both devices

Transport additions, the link manager, WiFi manager changes and removal of the WiFi watchdog's reboot. No behaviour changes yet in either firmware's main loop beyond the watchdog no longer rebooting; Tasks 6 and 7 wire the new pieces in.

**Files:**
- Modify: `include/controller/transport/espnow_controller_transport.h`, `src/controller/transport/espnow_controller_transport.cpp`
- Modify: `include/thermostat/transport/espnow_thermostat_transport.h`, `src/thermostat/transport/espnow_thermostat_transport.cpp`
- Modify: `include/thermostat/thermostat_device_runtime.h`
- Create: `include/transport/espnow_link_manager.h`
- Modify: `include/wifi_provisioning_manager.h`, `src/wifi_provisioning_manager.cpp`
- Modify: `include/wifi_watchdog.h`
- Modify: `src/esp32_controller_main.cpp` (remove the one `wifi_watchdog_set_reboot_hook` call)

**Interfaces:**
- Consumes: `EspNowChannelPolicy`, `EspNowChannelConfig`, `ChannelInputs`, `ChannelAction`, `ChannelActionType` (Task 2).
- Produces:
  - On both `EspNowControllerTransport` and `EspNowThermostatTransport`: `bool peer_heard() const`, `uint32_t last_peer_rx_ms() const`, `void send_probe()`, `bool has_unicast_peer() const`. On `EspNowThermostatTransport` also `bool restart()`.
  - `EspNowThermostatTransport &ThermostatDeviceRuntime::transport()`
  - `template <typename Transport> class thermostat::EspNowLinkManager` with `void begin(const EspNowChannelConfig &, Transport *)`, `void tick(uint32_t now_ms, bool mqtt_up, bool enabled)`, `bool linked() const`, `bool wifi_suspended() const`, `uint8_t channel() const`, `uint32_t search_count() const`, `uint32_t wifi_retry_interval_ms() const`
  - `WifiProvisioningManager::set_retry_interval_ms(uint32_t)`, `set_suspended(bool)`, `suspended() const`
  - `wifi_watchdog_set_reboot_hook` no longer exists.

- [ ] **Step 1: Controller transport header**

In `include/controller/transport/espnow_controller_transport.h`, add these public members after `const uint8_t *last_rx_mac() const { return last_rx_mac_; }`:

```cpp
  // True ESP-NOW liveness: set only when a frame from an authorized peer with a
  // compatible protocol version arrives over the radio. Unlike the app-level
  // heartbeat, this is never refreshed by MQTT.
  bool peer_heard() const { return peer_heard_; }
  uint32_t last_peer_rx_ms() const { return last_peer_rx_ms_; }
  // Send one heartbeat to every unicast peer immediately, bypassing the send throttle.
  // Used as a channel probe: a radio-level acknowledgement increments send_ok_count().
  void send_probe();
  bool has_unicast_peer() const;
```

and these private members after `uint8_t last_rx_mac_[6] = {0};`:

```cpp
  bool peer_heard_ = false;
  uint32_t last_peer_rx_ms_ = 0;
```

Also replace the `NOTE:` paragraph in the `restart()` comment with:

```cpp
  // NOTE: re-pins the radio to the configured channel. On a WiFi-associated board call
  // it only when the device is isolated; the link manager restores the wanted channel.
```

- [ ] **Step 2: Controller transport implementation**

In `src/controller/transport/espnow_controller_transport.cpp`:

Add to the top of the file, inside `namespace thermostat {` and before `EspNowControllerTransport *EspNowControllerTransport::instance_ = nullptr;`:

```cpp
namespace {
bool is_unicast_mac(const uint8_t mac[6]) {
  static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return !espnow_peer_filter::is_all_zero_mac(mac) && memcmp(mac, kBroadcast, 6) != 0;
}
}  // namespace
```

In `on_recv`, directly after the `is_compatible_protocol_version` check returns (before `const auto packet_type = ...`), add:

```cpp
  peer_heard_ = true;
#if defined(ARDUINO)
  last_peer_rx_ms_ = millis();
#endif
```

Add these two methods after `send_heartbeat`:

```cpp
bool EspNowControllerTransport::has_unicast_peer() const {
  for (int i = 0; i < config_.peer_count; ++i) {
    if (is_unicast_mac(config_.peer_macs[i])) return true;
  }
  return false;
}

void EspNowControllerTransport::send_probe() {
  if (!initialized_) {
    return;
  }
  HeartbeatPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::Heartbeat);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.toggle = heartbeat_toggle_ ? 1 : 0;
#if defined(ARDUINO)
  for (int i = 0; i < config_.peer_count; ++i) {
    if (is_unicast_mac(config_.peer_macs[i])) {
      esp_now_send(config_.peer_macs[i], reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
    }
  }
#else
  (void)pkt;
#endif
}
```

- [ ] **Step 3: Thermostat transport header and implementation**

In `include/thermostat/transport/espnow_thermostat_transport.h`, add after `bool begin(const EspNowThermostatConfig &config);`:

```cpp
  // Tear down and re-initialise ESP-NOW with the stored config. Re-pins the radio to the
  // configured channel; the link manager restores the wanted channel afterwards.
  bool restart();
```

add after `uint32_t send_fail_count() const { return send_fail_count_; }`:

```cpp
  // True ESP-NOW liveness: set only when a frame from the configured peer arrives.
  bool peer_heard() const { return peer_heard_; }
  uint32_t last_peer_rx_ms() const { return last_peer_rx_ms_; }
  // Send one heartbeat immediately, bypassing the send throttle (channel probe).
  void send_probe();
  bool has_unicast_peer() const;
```

and add private members after `uint32_t send_fail_count_ = 0;`:

```cpp
  bool peer_heard_ = false;
  uint32_t last_peer_rx_ms_ = 0;
```

In `src/thermostat/transport/espnow_thermostat_transport.cpp`:

Add after the `begin` function:

```cpp
bool EspNowThermostatTransport::restart() {
#if defined(ARDUINO)
  esp_now_deinit();
#endif
  initialized_ = false;
  const EspNowThermostatConfig config = config_;
  return begin(config);
}

bool EspNowThermostatTransport::has_unicast_peer() const {
  static const uint8_t kZero[6] = {0, 0, 0, 0, 0, 0};
  return !is_broadcast_mac(config_.peer_mac) && memcmp(config_.peer_mac, kZero, 6) != 0;
}

void EspNowThermostatTransport::send_probe() {
  if (!initialized_ || !has_unicast_peer()) {
    return;
  }
  HeartbeatPacket pkt;
  pkt.header.type = static_cast<uint8_t>(PacketType::Heartbeat);
  pkt.header.version = kEspNowProtocolVersion;
  pkt.toggle = heartbeat_toggle_ ? 1 : 0;
#if defined(ARDUINO)
  esp_now_send(config_.peer_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
#else
  (void)pkt;
#endif
}
```

In `on_recv`, directly after the `is_compatible_protocol_version` check (before `const auto packet_type = ...`), add:

```cpp
  peer_heard_ = true;
#if defined(ARDUINO)
  last_peer_rx_ms_ = millis();
#endif
```

In `include/thermostat/thermostat_device_runtime.h`, add after `uint32_t espnow_send_fail_count() const;`:

```cpp
  EspNowThermostatTransport &transport() { return node_.transport(); }
```

- [ ] **Step 4: Create the link manager**

`include/transport/espnow_link_manager.h`:

```cpp
#pragma once
#ifdef ARDUINO

#include <WiFi.h>
#include <esp_wifi.h>
#include <stdint.h>

#include "transport/espnow_channel_policy.h"

namespace thermostat {

// Runs EspNowChannelPolicy against the real radio. Shared by the controller and the
// display; Transport is EspNowControllerTransport or EspNowThermostatTransport and must
// provide peer_heard(), last_peer_rx_ms(), send_ok_count() and send_probe().
//
// Call tick() from the main loop task only (it sends frames and sets the channel).
template <typename Transport>
class EspNowLinkManager {
 public:
  void begin(const EspNowChannelConfig &config, Transport *transport) {
    policy_ = EspNowChannelPolicy(config);
    transport_ = transport;
  }

  // enabled: ESP-NOW is initialised and has a unicast peer to probe. When false the
  // link state is still tracked, but the channel is left alone.
  void tick(uint32_t now_ms, bool mqtt_up, bool enabled) {
    if (transport_ == nullptr) return;

    ChannelInputs in;
    in.now_ms = now_ms;
    in.wifi_associated = WiFi.status() == WL_CONNECTED;
    in.wifi_channel = in.wifi_associated ? static_cast<uint8_t>(WiFi.channel()) : 0;
    in.mqtt_up = mqtt_up;
    in.peer_heard = transport_->peer_heard();
    in.last_peer_rx_ms = transport_->last_peer_rx_ms();
    linked_ = policy_.linked(in);
    if (!enabled) return;

    // Any send acknowledged since the probe went out proves the peer is on this channel.
    if (probe_outstanding_ && transport_->send_ok_count() != probe_ok_baseline_) {
      probe_outstanding_ = false;
      policy_.on_probe_result(true);
    }

    const ChannelAction action = policy_.poll(in);
    switch (action.type) {
      case ChannelActionType::SetChannel:
        probe_outstanding_ = false;
        esp_wifi_set_channel(action.channel, WIFI_SECOND_CHAN_NONE);
        break;
      case ChannelActionType::Probe:
        probe_ok_baseline_ = transport_->send_ok_count();
        probe_outstanding_ = true;
        transport_->send_probe();
        break;
      case ChannelActionType::SuspendWifi:
      case ChannelActionType::ResumeWifi:
      case ChannelActionType::None:
      default:
        break;
    }

    // A WiFi join attempt scans and leaves the radio wherever it stopped. While not
    // associated, put it back on the channel the policy wants, once a second.
    if (!in.wifi_associated && static_cast<uint32_t>(now_ms - last_enforce_ms_) >= 1000) {
      last_enforce_ms_ = now_ms;
      uint8_t primary = 0;
      wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
      const uint8_t want = policy_.current_channel();
      if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != want) {
        esp_wifi_set_channel(want, WIFI_SECOND_CHAN_NONE);
      }
    }
  }

  bool linked() const { return linked_; }
  bool wifi_suspended() const { return policy_.wifi_suspended(); }
  uint8_t channel() const { return policy_.current_channel(); }
  uint32_t search_count() const { return policy_.search_count(); }
  // Join scans deafen the radio for seconds, so back off while ESP-NOW carries the link.
  uint32_t wifi_retry_interval_ms() const { return linked_ ? 60000UL : 5000UL; }

 private:
  EspNowChannelPolicy policy_{};
  Transport *transport_ = nullptr;
  bool linked_ = false;
  bool probe_outstanding_ = false;
  uint32_t probe_ok_baseline_ = 0;
  uint32_t last_enforce_ms_ = 0;
};

}  // namespace thermostat

#endif  // ARDUINO
```

- [ ] **Step 5: WiFi provisioning manager**

In `include/wifi_provisioning_manager.h`, add after `void request_reconnect() { m_reconnect_required = true; }`:

```cpp
  // Change how often ensure_connected() retries WiFi.begin() while disconnected.
  void set_retry_interval_ms(uint32_t ms) { m_config.retry_interval_ms = ms; }

  // While suspended, ensure_connected() keeps the STA disconnected and makes no join
  // attempts, leaving the radio free for ESP-NOW. Clearing it retries immediately.
  void set_suspended(bool suspended);
  bool suspended() const { return m_suspended; }
```

and add `bool m_suspended = false;` after `bool m_wifi_radio_started = false;`.

In `src/wifi_provisioning_manager.cpp`:

Change `WiFi.setAutoReconnect(true);` in `ensure_wifi_radio()` to:

```cpp
  // This manager owns every join attempt. With driver auto-reconnect on, the radio
  // would scan on its own and pull an ESP-NOW link off its channel.
  WiFi.setAutoReconnect(false);
```

Add after `clear_credentials()`:

```cpp
void WifiProvisioningManager::set_suspended(bool suspended) {
  if (m_suspended == suspended) return;
  m_suspended = suspended;
  if (!suspended) {
    m_last_attempt_ms = 0;  // retry on the next ensure_connected()
  }
}
```

In `ensure_connected`, insert directly before `if (WiFi.status() == WL_CONNECTED) return;`:

```cpp
  if (m_suspended) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect();  // keeps the radio on (ESP-NOW still works)
    }
    return;
  }
```

- [ ] **Step 6: Remove the WiFi watchdog's reboot**

In `include/wifi_watchdog.h`:

1. Replace the header comment's last sentence ("If the gateway is unreachable for an extended period, forces WiFi reconnect or full reboot.") with: "If the gateway is unreachable for 2 minutes it forces a WiFi reconnect, and repeats every 2 minutes while it stays unreachable. It never reboots: NetworkRebootGovernor is the only network reboot authority."
2. Delete the constants `REBOOT_THRESHOLD_MS` and `MAX_RECONNECTS`.
3. Delete `static void (*g_reboot_hook)(const char *reason) = nullptr;` and its comment.
4. Delete the whole `wifi_watchdog_set_reboot_hook` function and its comment.
5. In `handle_ping_result`, replace everything from `uint32_t failure_duration = now_ms - g_state.first_failure_ms;` to the end of the function with:

```cpp
    uint32_t failure_duration = now_ms - g_state.first_failure_ms;

    if (failure_duration >= RECONNECT_THRESHOLD_MS && !g_state.reconnect_attempted) {
        g_state.reconnect_attempted = true;
        g_state.watchdog_disconnected = true;
        g_state.reconnect_count++;
        // Restart the failure clock so the next forced reconnect is at least
        // RECONNECT_THRESHOLD_MS away.
        g_state.first_failure_ms = now_ms;
        log_state("forcing_wifi_reconnect", now_ms);
        WiFi.disconnect();
        WiFi.reconnect();
    }
}
```

In `src/esp32_controller_main.cpp`, delete the line `wifi_watchdog_set_reboot_hook(&ctrl_persist_reboot_reason);` (in `setup()`, just after the boot log `Serial.printf` that prints reset and reboot reasons).

- [ ] **Step 7: Build everything**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: `failures: 0`.

Run: `pio run -e esp32-furnace-controller -e esp32-furnace-thermostat -e esp32-furnace-controller-waveshare-prod`
Expected: all three SUCCESS. If the thermostat env fails to link `EspNowChannelPolicy` symbols, its `build_src_filter` in `platformio.ini` excludes `src/transport/`; add `+<transport/*>` to that env's filter and rebuild.

- [ ] **Step 8: Commit**

```bash
git add include/controller/transport/espnow_controller_transport.h src/controller/transport/espnow_controller_transport.cpp include/thermostat/transport/espnow_thermostat_transport.h src/thermostat/transport/espnow_thermostat_transport.cpp include/thermostat/thermostat_device_runtime.h include/transport/espnow_link_manager.h include/wifi_provisioning_manager.h src/wifi_provisioning_manager.cpp include/wifi_watchdog.h src/esp32_controller_main.cpp platformio.ini
git commit -m "feat(transport): true ESP-NOW liveness, channel probe, link manager; drop WiFi watchdog reboot"
```

---

### Task 6: Controller integration

**Files:**
- Modify: `src/esp32_controller_main.cpp`
- Modify: `include/discovery_topics.h`
- Test: `src/tests/test_mqtt_discovery.cpp` (update expected controller entity count if it asserts one)

**Interfaces:**
- Consumes: `EspNowLinkManager<EspNowControllerTransport>` (Task 5), `NetworkRebootGovernor`, `GovernorAction`, `NetworkRebootConfig` (Task 3), `RecentFailWindow` (Task 4), `EspNowChannelConfig`, `LinkRole` (Task 2), transport `last_peer_rx_ms()`, `has_unicast_peer()`, `restart()` (Task 5), `WifiProvisioningManager::set_retry_interval_ms` (Task 5).
- Produces: MQTT topics under the controller's `state/`: `espnow_channel`, `connection_path`, `espnow_search_count`, `net_reboot_count`, `wifi_disconnect_reason`; `/status` JSON fields `espnow_channel`, `connection_path`, `espnow_search_count`, `net_reboot_count`, `net_reboot_in_s`, `wifi_disconnect_reason`; NVS key `net_rb_cnt` in the `g_ctrl_cfg` namespace.

- [ ] **Step 1: Includes and globals**

Near the other project includes at the top of `src/esp32_controller_main.cpp`, add:

```cpp
#include <esp_now.h>

#include "network_reboot_governor.h"
#include "transport/espnow_fail_window.h"
#include "transport/espnow_link_manager.h"
```

Replace these two globals:

```cpp
uint32_t g_ctrl_isolation_start_ms = 0;  // 0 = not isolated
bool g_ctrl_isolation_recovery_done = false;  // in-place subsystem recovery already tried this episode
```

with:

```cpp
// ESP-NOW channel agreement + network reboot ladder. See
// docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md.
thermostat::EspNowLinkManager<thermostat::EspNowControllerTransport> g_ctrl_link;
thermostat::NetworkRebootGovernor g_ctrl_reboot_gov;
thermostat::RecentFailWindow g_ctrl_espnow_fail_window;
// Reason code of the most recent drop of an established WiFi association (0 = none).
std::atomic<uint8_t> g_ctrl_wifi_drop_reason{0};
std::atomic<bool> g_ctrl_wifi_was_connected{false};
std::atomic<bool> g_ctrl_wifi_drop_audit_pending{false};
```

Delete the constants `kCtrlIsolationRebootMs` and `kCtrlIsolationRecoveryGraceMs` together with their comments.

- [ ] **Step 2: Record the WiFi disconnect reason**

In `ctrl_wifi_event_handler`, replace the `ARDUINO_EVENT_WIFI_STA_GOT_IP` case with:

```cpp
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_ctrl_wifi_was_connected.store(true);
      g_ctrl_wifi.on_wifi_connected();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // Failed join attempts also raise this event (reason 201, every retry). Record
      // only the drop of an association that was actually up.
      if (g_ctrl_wifi_was_connected.exchange(false)) {
        g_ctrl_wifi_drop_reason.store(event->event_info.wifi_sta_disconnected.reason);
        g_ctrl_wifi_drop_audit_pending.store(true);
      }
      break;
```

- [ ] **Step 3: In-place recovery and connection-path helpers**

Add directly above `void ctrl_ensure_wifi_connected(uint32_t now_ms) {`:

```cpp
const char *ctrl_connection_path() {
  const bool mqtt = g_ctrl_mqtt_up.load();
  const bool espnow = g_ctrl_link.linked();
  return mqtt && espnow ? "mqtt+esp-now" : mqtt ? "mqtt" : espnow ? "esp-now" : "disconnected";
}

// Re-initialise the network subsystems without rebooting. Called by the reboot
// governor only when the device is isolated (no MQTT, no ESP-NOW peer), so there is no
// working link to disturb.
void ctrl_recover_network_in_place() {
  bool espnow_ok = true;
#if defined(CONTROLLER_BOARD_WAVESHARE)
  // WiFi is never associated on this board; Ethernet/lwIP self-recovers.
  if (g_cfg_ctrl_espnow_enabled) espnow_ok = g_controller->transport().restart();
#else
  // Classic board: WiFi carries IP and shares the radio with ESP-NOW. Power-cycle the
  // radio, bring ESP-NOW back, and let the WiFi manager rejoin.
  esp_now_deinit();
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  if (g_cfg_ctrl_espnow_enabled) espnow_ok = g_controller->transport().restart();
  g_ctrl_wifi.request_reconnect();
#endif
  g_ctrl_mqtt_recovery.on_disconnected();   // reset backoff -> immediate reconnect
  g_ctrl_mqtt_reconfigure_required = true;  // MQTT task disconnects + reconnects cleanly
  ctrl_audit("isolation: recovered in place (espnow_reinit=%d)", espnow_ok ? 1 : 0);
}
```

- [ ] **Step 4: Load the persisted reboot count and start the link manager**

In `setup()`, directly after the block that loads `g_ctrl_reboot_reason` from NVS (the `Serial.printf` that prints reset and reboot reasons), add:

```cpp
  {
    const uint32_t net_reboots =
        g_ctrl_cfg_ready ? g_ctrl_cfg.getUInt("net_rb_cnt", 0U) : 0U;
    g_ctrl_reboot_gov =
        thermostat::NetworkRebootGovernor(thermostat::NetworkRebootConfig(), net_reboots);
    Serial.printf("[boot] consecutive network reboots=%lu\n",
                  static_cast<unsigned long>(net_reboots));
  }
```

In `setup()`, directly after the line `g_ctrl_last_espnow_error = ok ? "none" : (g_cfg_ctrl_espnow_enabled ? "begin_failed" : "disabled");`, add:

```cpp
  {
    thermostat::EspNowChannelConfig link_cfg;
    link_cfg.role = thermostat::LinkRole::Controller;
    link_cfg.rendezvous_channel = g_cfg_ctrl_espnow_channel;
    link_cfg.search_interval_ms = 20000;
    g_ctrl_link.begin(link_cfg, &g_controller->transport());
  }
```

- [ ] **Step 5: Run the link manager and WiFi backoff in `loop()`**

In `loop()`, replace:

```cpp
    ctrl_ensure_wifi_connected(now);
    wifi_watchdog_tick(now, g_ctrl_mqtt_up.load());
```

with:

```cpp
    g_ctrl_wifi.set_retry_interval_ms(g_ctrl_link.wifi_retry_interval_ms());
    ctrl_ensure_wifi_connected(now);
    wifi_watchdog_tick(now, g_ctrl_mqtt_up.load());
```

Directly after the closing `#endif` of that `#if !defined(CONTROLLER_BOARD_WAVESHARE)` block (so it runs on both boards), add:

```cpp
    g_ctrl_link.tick(now, g_ctrl_mqtt_up.load(),
                     g_cfg_ctrl_espnow_enabled && g_controller->transport().has_unicast_peer());
    if (g_ctrl_wifi_drop_audit_pending.exchange(false)) {
      ctrl_audit("wifi: association dropped, reason=%u",
                 static_cast<unsigned>(g_ctrl_wifi_drop_reason.load()));
    }
```

- [ ] **Step 6: Replace the isolation reboot block with the governor**

In `loop()`, delete the entire block that starts with the comment `// Isolation reboot: if both MQTT and ESP-NOW have been down continuously` and ends with the closing brace of that scope (the block containing `s_network_was_ready`, `g_ctrl_isolation_start_ms` and both `ESP.restart()` calls). Put this in its place:

```cpp
    // Network isolation: neither MQTT nor the ESP-NOW peer is reachable. The governor
    // tries an in-place recovery first, then reboots on a backed-off ladder (10 min,
    // 1 h, 4 h, then daily) so a long outage cannot become a reboot loop.
    {
      static bool s_network_was_ready = false;
      if (ctrl_network_ready()) s_network_was_ready = true;
      const bool isolated = !g_ctrl_mqtt_up.load() && !g_ctrl_link.linked();
      switch (g_ctrl_reboot_gov.poll(now, isolated, s_network_was_ready)) {
        case thermostat::GovernorAction::RecoverInPlace:
          ctrl_recover_network_in_place();
          break;
        case thermostat::GovernorAction::Reboot: {
          const uint32_t count = g_ctrl_reboot_gov.reboot_count();
          if (g_ctrl_cfg_ready) g_ctrl_cfg.putUInt("net_rb_cnt", count);
          char reason[80];
          snprintf(reason, sizeof(reason), "isolation: mqtt+espnow down, net_reboot=%lu",
                   static_cast<unsigned long>(count));
          ctrl_audit("isolation_reboot: %s", reason);
          ctrl_persist_reboot_reason(reason);
          ESP.restart();
          break;
        }
        case thermostat::GovernorAction::None:
        default:
          break;
      }
      if (g_ctrl_reboot_gov.take_count_reset() && g_ctrl_cfg_ready) {
        g_ctrl_cfg.putUInt("net_rb_cnt", 0U);
        ctrl_audit("isolation: network healthy 10 min, reboot count reset");
      }
    }
```

- [ ] **Step 7: Publish the diagnostics**

In `ctrl_publish_runtime_state()`:

Replace the two lines that publish `state/last_espnow_rx_ms` from `rt.heartbeat_last_seen_ms()` with:

```cpp
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_controller->transport().last_peer_rx_ms()));
  g_ctrl_mqtt.publish(self_topic_for("state/last_espnow_rx_ms").c_str(), buf, true);
```

Replace:

```cpp
  if (g_ctrl_last_espnow_error != "begin_failed") {
    g_ctrl_last_espnow_error =
        g_controller->transport().send_fail_count() > 0 ? "send_failed" : "none";
  }
```

with:

```cpp
  g_ctrl_espnow_fail_window.update(millis(), g_controller->transport().send_fail_count());
  if (g_ctrl_last_espnow_error != "begin_failed") {
    g_ctrl_last_espnow_error =
        g_ctrl_espnow_fail_window.recent(millis()) ? "send_failed" : "none";
  }
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_ctrl_link.channel()));
  g_ctrl_mqtt.publish(self_topic_for("state/espnow_channel").c_str(), buf, true);
  g_ctrl_mqtt.publish(self_topic_for("state/connection_path").c_str(),
                      ctrl_connection_path(), true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_ctrl_link.search_count()));
  g_ctrl_mqtt.publish(self_topic_for("state/espnow_search_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu",
           static_cast<unsigned long>(g_ctrl_reboot_gov.reboot_count()));
  g_ctrl_mqtt.publish(self_topic_for("state/net_reboot_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_ctrl_wifi_drop_reason.load()));
  g_ctrl_mqtt.publish(self_topic_for("state/wifi_disconnect_reason").c_str(), buf, true);
```

In `ctrl_web_handle_status_get()`:

Replace the `espnow_connected` argument

```cpp
    (rt.heartbeat_last_seen_ms() > 0 && (now - rt.heartbeat_last_seen_ms()) < 30000UL) ? "true" : "false",
```

with:

```cpp
    g_ctrl_link.linked() ? "true" : "false",
```

In the splice block that appends `dns0`, `dns1` and `mac`, replace the `snprintf` with:

```cpp
      snprintf(buf + len, sizeof(buf) - len,
        ","
        "\"dns0\":\"%s\","
        "\"dns1\":\"%s\","
        "\"mac\":\"%s\","
        "\"espnow_channel\":%u,"
        "\"connection_path\":\"%s\","
        "\"espnow_search_count\":%lu,"
        "\"net_reboot_count\":%lu,"
        "\"net_reboot_in_s\":%lu,"
        "\"wifi_disconnect_reason\":%u"
        "}",
        WiFi.dnsIP(0).toString().c_str(),
        WiFi.dnsIP(1).toString().c_str(),
        WiFi.macAddress().c_str(),
        static_cast<unsigned>(g_ctrl_link.channel()),
        ctrl_connection_path(),
        static_cast<unsigned long>(g_ctrl_link.search_count()),
        static_cast<unsigned long>(g_ctrl_reboot_gov.reboot_count()),
        static_cast<unsigned long>(g_ctrl_reboot_gov.ms_until_reboot(millis()) / 1000UL),
        static_cast<unsigned>(g_ctrl_wifi_drop_reason.load()));
```

Check the declared size of `buf` in that function. The added text is at most 190 bytes; if `sizeof(buf)` minus the longest existing response leaves less than that, raise the buffer size by 256.

- [ ] **Step 8: Home Assistant discovery**

In `ctrl_publish_discovery()`, directly after the block that publishes `err_espnow_topic`, add:

```cpp
  {
    struct Diag {
      const char *key;
      const char *name;
      const char *state;
    };
    static const Diag kLinkDiags[] = {
        {"espnow_channel", "Controller ESP-NOW Channel", "espnow_channel"},
        {"connection_path", "Controller Connection Path", "connection_path"},
        {"espnow_search_count", "Controller ESP-NOW Search Count", "espnow_search_count"},
        {"net_reboot_count", "Controller Network Reboot Count", "net_reboot_count"},
        {"wifi_disconnect_reason", "Controller WiFi Disconnect Reason", "wifi_disconnect_reason"},
    };
    for (const Diag &d : kLinkDiags) {
      const String topic = dp + "sensor/" + dev_id + "_controller_" + d.key + "/config";
      snprintf(payload, sizeof(payload),
               "{\"name\":\"%s\",\"uniq_id\":\"%s_controller_%s\","
               "\"stat_t\":\"%s/state/%s\",\"entity_category\":\"diagnostic\","
               "\"dev\":{\"ids\":[\"%s\"]}}",
               d.name, dev_id.c_str(), d.key, base.c_str(), d.state, dev_id.c_str());
      g_ctrl_mqtt.publish(topic.c_str(), payload, true);
    }
  }
```

In `include/discovery_topics.h`, change the comment `// Controller entities (28 total)` to `// Controller entities (33 total)` and add after `{"sensor", "_controller_error_espnow"},`:

```cpp
    {"sensor", "_controller_espnow_channel"},
    {"sensor", "_controller_connection_path"},
    {"sensor", "_controller_espnow_search_count"},
    {"sensor", "_controller_net_reboot_count"},
    {"sensor", "_controller_wifi_disconnect_reason"},
```

- [ ] **Step 9: Build and run tests**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: `failures: 0`. If a test in `src/tests/test_mqtt_discovery.cpp` fails because it asserts the controller entity count is 28, change that expected value to 33 and re-run.

Run: `pio run -e esp32-furnace-controller -e esp32-furnace-controller-waveshare-prod`
Expected: both SUCCESS, with no remaining references to `g_ctrl_isolation_start_ms`, `kCtrlIsolationRebootMs` or `wifi_watchdog_set_reboot_hook`.

Run: `rg -n "g_ctrl_isolation|kCtrlIsolation|wifi_watchdog_set_reboot_hook" src include`
Expected: no matches.

- [ ] **Step 10: Commit**

```bash
git add src/esp32_controller_main.cpp include/discovery_topics.h src/tests/test_mqtt_discovery.cpp
git commit -m "feat(controller): follow the ESP-NOW peer's channel and back off isolation reboots"
```

---

### Task 7: Display integration

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp`
- Modify: `include/discovery_topics.h`
- Test: `src/tests/test_mqtt_discovery.cpp` (update expected display entity count if it asserts one)

**Interfaces:**
- Consumes: `EspNowLinkManager<EspNowThermostatTransport>` (Task 5), `NetworkRebootGovernor`, `GovernorAction`, `NetworkRebootConfig` (Task 3), `RecentFailWindow` (Task 4), `EspNowChannelConfig`, `LinkRole` (Task 2), `ThermostatDeviceRuntime::transport()`, transport `has_unicast_peer()` and `restart()`, `WifiProvisioningManager::set_retry_interval_ms` and `set_suspended` (Task 5).
- Produces: MQTT topics under the display's `state/`: `espnow_channel`, `espnow_search_count`, `net_reboot_count`, `wifi_disconnect_reason`; `/status` JSON fields `espnow_channel`, `wifi_suspended`, `espnow_search_count`, `net_reboot_count`, `net_reboot_in_s`, `wifi_disconnect_reason`; NVS key `net_rb_cnt` in the `cfg_disp` namespace.

- [ ] **Step 1: Includes and globals**

Near the other project includes at the top of `src/thermostat/esp32s3_thermostat_firmware.cpp`, add:

```cpp
#include <atomic>
#include <esp_now.h>

#include "network_reboot_governor.h"
#include "transport/espnow_fail_window.h"
#include "transport/espnow_link_manager.h"
```

Replace:

```cpp
uint32_t g_isolation_start_ms = 0;   // when both MQTT+ESP-NOW went down (0=not isolated)
```

with:

```cpp
// ESP-NOW channel agreement + network reboot ladder. See
// docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md.
thermostat::EspNowLinkManager<thermostat::EspNowThermostatTransport> g_link;
thermostat::NetworkRebootGovernor g_reboot_gov;
thermostat::RecentFailWindow g_espnow_fail_window;
std::atomic<uint8_t> g_wifi_drop_reason{0};   // reason code of the last dropped association
std::atomic<bool> g_wifi_was_connected{false};
std::atomic<bool> g_wifi_connected_once{false};
```

Delete the constant `kIsolationRebootMs`.

- [ ] **Step 2: Record the WiFi disconnect reason**

Replace the body of `wifi_event_handler` with:

```cpp
void wifi_event_handler(arduino_event_t *event) {
  if (event == nullptr) return;
  if (event->event_id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    g_wifi_was_connected.store(true);
    g_wifi_connected_once.store(true);
    g_wifi.on_wifi_connected();
  } else if (event->event_id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // Failed join attempts also raise this event. Record only the drop of an
    // association that was actually up.
    if (g_wifi_was_connected.exchange(false)) {
      g_wifi_drop_reason.store(event->event_info.wifi_sta_disconnected.reason);
      Serial.printf("[wifi] association dropped, reason=%u\n",
                    static_cast<unsigned>(g_wifi_drop_reason.load()));
    }
  }
}
```

- [ ] **Step 3: In-place recovery helper**

Add directly above `void ensure_wifi_connected(uint32_t now_ms) {`:

```cpp
// Re-initialise the network subsystems without rebooting. Called by the reboot governor
// only when the display is isolated (no MQTT, no ESP-NOW peer).
void recover_network_in_place() {
  if (g_runtime == nullptr) return;
  bool espnow_ok = true;
  xSemaphoreTake(g_api_mutex, portMAX_DELAY);
  if (g_cfg_wifi_disabled) {
    if (g_cfg_espnow_enabled) espnow_ok = g_runtime->transport().restart();
  } else {
    esp_now_deinit();
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    if (g_cfg_espnow_enabled) espnow_ok = g_runtime->transport().restart();
    g_wifi.request_reconnect();
    g_cfg_mqtt_reconfigure_required = true;
  }
  xSemaphoreGive(g_api_mutex);
  Serial.printf("[watchdog] isolation: recovered in place (espnow_reinit=%d)\n",
                espnow_ok ? 1 : 0);
}
```

- [ ] **Step 4: Load the persisted reboot count and start the link manager**

In `thermostat_firmware_setup()`, directly after the lines that compute and store `g_boot_count` (`g_cfg.putUInt("boot_cnt", g_boot_count);` and its enclosing `if`), add:

```cpp
  {
    const uint32_t net_reboots = g_cfg_ready ? g_cfg.getUInt("net_rb_cnt", 0U) : 0U;
    g_reboot_gov =
        thermostat::NetworkRebootGovernor(thermostat::NetworkRebootConfig(), net_reboots);
    Serial.printf("[boot] consecutive network reboots=%lu\n",
                  static_cast<unsigned long>(net_reboots));
  }
```

Directly after the line `g_last_espnow_error = runtime_ok ? "none" : (g_cfg_espnow_enabled ? "begin_failed" : "disabled");`, add:

```cpp
  {
    thermostat::EspNowChannelConfig link_cfg;
    link_cfg.role = thermostat::LinkRole::Display;
    link_cfg.rendezvous_channel = g_cfg_espnow_channel;
    link_cfg.search_interval_ms = 5000;
    g_link.begin(link_cfg, &g_runtime->transport());
  }
```

- [ ] **Step 5: Run the link manager, WiFi backoff and suspend in the loop**

In `thermostat_firmware_loop()`, replace:

```cpp
  if (!g_cfg_wifi_disabled) {
    ensure_wifi_connected(now);
```

with:

```cpp
  // Runs in both normal and ESP-NOW-only mode: when the radio is not associated it
  // follows the controller's channel (searching 1/6/11 if the controller goes quiet).
  if (g_runtime != nullptr) {
    g_link.tick(now, g_mqtt.connected(),
                g_cfg_espnow_enabled && g_runtime->transport().has_unicast_peer());
  }

  if (!g_cfg_wifi_disabled) {
    g_wifi.set_retry_interval_ms(g_link.wifi_retry_interval_ms());
    g_wifi.set_suspended(g_link.wifi_suspended());
    ensure_wifi_connected(now);
```

- [ ] **Step 6: Replace the isolation reboot block with the governor**

In `thermostat_firmware_loop()`, find the block that starts with the comment `// Isolation reboot: if both MQTT and ESP-NOW have been down continuously`. Keep the `[espnow] controller CONNECTED/disconnected` transition logging, and replace everything from `const bool isolated = !g_mqtt.connected() && !espnow_active;` through the end of that scope with:

```cpp
    // The governor tries an in-place recovery first, then reboots on a backed-off
    // ladder (10 min, 1 h, 4 h, then daily) so a long outage cannot loop.
    const bool isolated = !g_mqtt.connected() && !g_link.linked();
    const bool network_was_ready = g_wifi_connected_once.load() || g_cfg_wifi_disabled;
    switch (g_reboot_gov.poll(now, isolated, network_was_ready)) {
      case thermostat::GovernorAction::RecoverInPlace:
        recover_network_in_place();
        break;
      case thermostat::GovernorAction::Reboot: {
        const uint32_t count = g_reboot_gov.reboot_count();
        if (g_cfg_ready) g_cfg.putUInt("net_rb_cnt", count);
        Serial.printf("[watchdog] isolation_reboot: mqtt+espnow down, net_reboot=%lu\n",
                      static_cast<unsigned long>(count));
        Serial.flush();
        shutdown_display_for_reboot();
        delay(kRebootPanelOffDelayMs);
        ESP.restart();
        break;
      }
      case thermostat::GovernorAction::None:
      default:
        break;
    }
    if (g_reboot_gov.take_count_reset() && g_cfg_ready) {
      g_cfg.putUInt("net_rb_cnt", 0U);
      Serial.println("[watchdog] network healthy 10 min, reboot count reset");
    }
  }
```

Also change the comment above that scope to `// Network isolation: neither MQTT nor the controller's ESP-NOW heartbeat is reachable.`

- [ ] **Step 7: Publish the diagnostics**

In `mqtt_publish_state()`, replace:

```cpp
  if (g_last_espnow_error != "begin_failed") {
    g_last_espnow_error = g_runtime->espnow_send_fail_count() > 0 ? "send_failed" : "none";
  }
```

with:

```cpp
  g_espnow_fail_window.update(millis(), g_runtime->espnow_send_fail_count());
  if (g_last_espnow_error != "begin_failed") {
    g_last_espnow_error = g_espnow_fail_window.recent(millis()) ? "send_failed" : "none";
  }
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_link.channel()));
  g_mqtt.publish(self_topic_for("state/espnow_channel").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_link.search_count()));
  g_mqtt.publish(self_topic_for("state/espnow_search_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_reboot_gov.reboot_count()));
  g_mqtt.publish(self_topic_for("state/net_reboot_count").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(g_wifi_drop_reason.load()));
  g_mqtt.publish(self_topic_for("state/wifi_disconnect_reason").c_str(), buf, true);
```

In the `/status` handler (the function whose `snprintf` builds the JSON beginning `"\"uptime_ms\":%lu,"`):

Replace the `espnow_connected` argument

```cpp
    (espnow_hb_ms > 0 && (now - espnow_hb_ms) < 30000UL) ? "true" : "false",
```

with:

```cpp
    g_link.linked() ? "true" : "false",
```

Directly before `g_web.send(200, "application/json", buf);` in that function, add:

```cpp
  {
    // Link diagnostics, spliced in before the closing brace of the base response.
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '}') {
      len -= 1;
      snprintf(buf + len, sizeof(buf) - len,
        ","
        "\"espnow_channel\":%u,"
        "\"wifi_suspended\":%s,"
        "\"espnow_search_count\":%lu,"
        "\"net_reboot_count\":%lu,"
        "\"net_reboot_in_s\":%lu,"
        "\"wifi_disconnect_reason\":%u"
        "}",
        static_cast<unsigned>(g_link.channel()),
        g_link.wifi_suspended() ? "true" : "false",
        static_cast<unsigned long>(g_link.search_count()),
        static_cast<unsigned long>(g_reboot_gov.reboot_count()),
        static_cast<unsigned long>(g_reboot_gov.ms_until_reboot(millis()) / 1000UL),
        static_cast<unsigned>(g_wifi_drop_reason.load()));
    }
  }
```

Check the declared size of `buf` in that function. The added text is at most 170 bytes; if the buffer has less headroom than that over the longest existing response, raise its size by 256.

- [ ] **Step 8: Home Assistant discovery**

In `mqtt_publish_discovery()`, directly after the block that publishes `err_espnow_config`, add:

```cpp
  {
    struct Diag {
      const char *key;
      const char *name;
      const char *state;
    };
    static const Diag kLinkDiags[] = {
        {"espnow_channel", "Display ESP-NOW Channel", "espnow_channel"},
        {"espnow_search_count", "Display ESP-NOW Search Count", "espnow_search_count"},
        {"net_reboot_count", "Display Network Reboot Count", "net_reboot_count"},
        {"wifi_disconnect_reason", "Display WiFi Disconnect Reason", "wifi_disconnect_reason"},
    };
    for (const Diag &d : kLinkDiags) {
      const String topic =
          g_cfg_discovery_prefix + "/sensor/" + node + "_display_" + d.key + "/config";
      snprintf(payload, sizeof(payload),
               "{\"name\":\"%s\",\"uniq_id\":\"%s_display_%s\","
               "\"stat_t\":\"%s/state/%s\",\"entity_category\":\"diagnostic\","
               "\"dev\":{\"ids\":[\"%s\"]}}",
               d.name, node.c_str(), d.key, base.c_str(), d.state, node.c_str());
      g_mqtt.publish(topic.c_str(), payload, true);
    }
  }
```

In `include/discovery_topics.h`, change the comment `// Display entities (19 total)` to `// Display entities (23 total)` and add after `{"sensor", "_display_error_espnow"},`:

```cpp
    {"sensor", "_display_espnow_channel"},
    {"sensor", "_display_espnow_search_count"},
    {"sensor", "_display_net_reboot_count"},
    {"sensor", "_display_wifi_disconnect_reason"},
```

- [ ] **Step 9: Build and run tests**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: `failures: 0`. If a test in `src/tests/test_mqtt_discovery.cpp` asserts the display entity count is 19, change that expected value to 23 and re-run.

Run: `pio run -e esp32-furnace-thermostat -e thermostat-bench-espnow -e esp32-furnace-controller`
Expected: all SUCCESS.

Run: `rg -n "g_isolation_start_ms|kIsolationRebootMs" src include`
Expected: no matches.

- [ ] **Step 10: Commit**

```bash
git add src/thermostat/esp32s3_thermostat_firmware.cpp include/discovery_topics.h src/tests/test_mqtt_discovery.cpp
git commit -m "feat(display): follow the controller's channel, leave WiFi when the broker is down, back off isolation reboots"
```

---

### Task 8: Bench validation and documentation

Needs bench hardware and Ryan. Uses the spare display (`80B54ED1B804`), the bench controller, and the isolated test broker (`10.0.2.175`, see `docs/superpowers/notes/2026-06-16-esp32s3-bench-validation-notes.md`). Bench builds use the `-test` environments (test base topic, test LMK), so they cannot affect production devices.

**Files:**
- Modify: `docs/hil-checklist.md`
- Modify: `docs/hil-results.md`
- Modify: `docs/espnow-transport.md`
- Modify: `AGENTS.md`
- Modify: `tasks/todo.md`

**Interfaces:**
- Consumes: firmware from Tasks 5-7; `/status` fields `espnow_channel`, `connection_path`, `espnow_search_count`, `net_reboot_count`, `net_reboot_in_s`, `wifi_disconnect_reason`, `espnow_connected`.
- Produces: recorded pass/fail for each scenario in `docs/hil-results.md`.

- [ ] **Step 1: Add the scenarios to the HIL checklist**

Append to `docs/hil-checklist.md`:

```markdown
## ESP-NOW fallback during WiFi outages (2026-10-13 design)

Setup: bench controller on `esp32-furnace-controller-test`, spare display on
`thermostat-bench-normal`, both paired by MAC with the test LMK, both joined to WiFi and
the test broker. Watch both serial logs and both `/status` pages.

- [ ] **F1 Baseline.** Both on WiFi, same AP. `/status` on both: `espnow_connected` true,
      `connection_path` `mqtt+esp-now`, `espnow_search_count` 0.
- [ ] **F2 Both lose WiFi.** Block both clients in UniFi. Within 60 s both report
      `espnow_connected` true again (`[espnow] controller CONNECTED` on serial), on the
      rendezvous channel (11 in the test builds, 6 in production). Change the setpoint on the display: the
      controller relays follow. No reboot in 9 minutes.
- [ ] **F3 Controller alone loses WiFi.** Unblock both, then block only the controller.
      Within 60 s the controller's serial shows the peer linked on the display's AP
      channel; display commands still move the relays.
- [ ] **F4 Display alone loses WiFi.** Mirror of F3.
- [ ] **F5 Broker down, different AP channels.** Lock the two devices to APs on different
      2.4 GHz channels in UniFi. Stop the test broker. Within 2 minutes the display logs a
      WiFi suspend, links over ESP-NOW, and display commands move the relays. Start the
      broker: within 6 minutes the display is back on WiFi and MQTT.
- [ ] **F6 Furnace failsafe not tripped.** During F2-F5 the controller's `failsafe_active`
      stays false.
- [ ] **F7 Reboot ladder.** Power off the display, block the controller in UniFi, leave it
      24 h. Unblock. `net_reboot_count` is 3 or less; `boot_count` rose by the same
      amount; reboot reasons read `isolation: mqtt+espnow down, net_reboot=N`. After 10
      healthy minutes `net_reboot_count` returns to 0.
- [ ] **F8 In-place recovery.** At the start of F7, the controller's audit log shows
      `isolation: recovered in place` 2 minutes after isolation begins.
- [ ] **F9 Cold boot in an outage.** With both blocked in UniFi, power-cycle both. They
      link over ESP-NOW within 60 s and neither reboots in 30 minutes.
- [ ] **F10 Waveshare.** Repeat F2 and F7 with the Waveshare controller, unplugging
      Ethernet instead of blocking WiFi.
- [ ] **F11 Disconnect reason.** After F3, once the controller is unblocked,
      `wifi_disconnect_reason` on `/status` is non-zero.
```

- [ ] **Step 2: Flash the bench devices and run F1-F11**

Run: `pio run -e esp32-furnace-controller-test -e thermostat-bench-normal -e esp32-furnace-controller-waveshare`
Expected: all SUCCESS.

Flash per the bench notes, then work through F1-F11. For each scenario record in `docs/hil-results.md`, under a new heading `## 2026-10-13 ESP-NOW fallback`: the date run, pass or fail, and the serial lines or `/status` values that show it.

Any failure is a bug: stop, use superpowers:systematic-debugging, fix in the owning task's files, add a native test if the fault is in a policy, and re-run the failed scenario and F1.

- [ ] **Step 3: Update the documentation**

In `docs/espnow-transport.md`, add this section before `## Remaining Work`:

```markdown
## Channel Agreement and WiFi Outages

ESP-NOW only works when both radios are on the same channel. While a device is joined
to WiFi its channel is the access point's, so the configured `espnow_channel` applies
only when neither device is on WiFi.

- A device that is **not** on WiFi follows its peer: it parks on the channel where it
  last heard the peer, and when the peer goes quiet for 30 s it probes that channel,
  then `espnow_channel` (the rendezvous channel), then the rest of 1, 6 and 11. Only
  those three channels are searched.
- When neither device is on WiFi they meet on `espnow_channel`.
- If the MQTT broker is down for 60 s while the display is on WiFi and cannot hear the
  controller, the display leaves WiFi to follow the controller, and retries WiFi every
  5 minutes.
- WiFi rejoin attempts slow from every 5 s to every 60 s while ESP-NOW carries the link.

Logic: `include/transport/espnow_channel_policy.h` (unit-tested). Radio glue:
`include/transport/espnow_link_manager.h`.

## Network Reboots

`NetworkRebootGovernor` (`include/network_reboot_governor.h`) is the only code that
reboots a device for network reasons. A device is isolated when MQTT is down and its
ESP-NOW peer has not been heard for 30 s. After 2 minutes isolated it re-initialises
ESP-NOW, WiFi and MQTT in place. It reboots after 10 minutes, then 1 hour, then 4 hours,
then at most once per 24 hours. The count is stored in NVS (`net_rb_cnt`) and resets
after 10 healthy minutes.
```

In `AGENTS.md`, replace the line `- ESP-NOW channel: `6` (both devices)` with:

```markdown
- ESP-NOW rendezvous channel: `6` (both devices). While on WiFi a device uses its AP's
  channel; off WiFi it follows its peer across channels 1, 6 and 11. See
  `docs/espnow-transport.md`.
```

In `tasks/todo.md`, add at the top, under `# TODO`:

```markdown
## ESP-NOW fallback during WiFi outages

Spec: `docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md`
Plan: `docs/superpowers/plans/2026-10-13-espnow-wifi-outage-fallback.md`
Production rollout to the live controller and display needs Ryan's go-ahead after the
bench list (F1-F11 in `docs/hil-checklist.md`) passes. Flash both devices together.
```

- [ ] **Step 4: Final verification**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: `failures: 0`.

Run: `pio run -e esp32-furnace-controller -e esp32-furnace-thermostat -e esp32-furnace-controller-waveshare-prod`
Expected: all SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add docs/hil-checklist.md docs/hil-results.md docs/espnow-transport.md AGENTS.md tasks/todo.md
git commit -m "docs: ESP-NOW fallback bench results, transport docs, rollout note"
```

---

## Production rollout (not a task; needs Ryan's explicit go-ahead)

1. Bench list F1-F11 all pass and are recorded.
2. Build `esp32-furnace-controller` and `esp32-furnace-thermostat` in one `pio run` invocation.
3. Flash the display first, then the controller, in the same sitting, at a time when a few minutes without heating or cooling is acceptable.
4. Confirm on both `/status` pages: `espnow_connected` true, `connection_path` `mqtt+esp-now`, `net_reboot_count` 0.
5. Watch the new Home Assistant diagnostics for a week, in particular `Controller WiFi Disconnect Reason`, which is the missing evidence for the 2026-10-13 radio dropout.
