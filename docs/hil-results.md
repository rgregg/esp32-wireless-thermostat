# HIL Results Log

Use this file to record each hardware validation run.

## Run Template
- Date:
- Operator:
- Controller firmware:
- Display firmware:
- Checklist reference: `docs/hil-checklist.md`
- Result: PASS / FAIL
- Notes:

---

## 2026-10-18 ESP-NOW fallback during WiFi outages (F0-F13)
- Date: 2026-10-18 (run in progress)
- Operator: Claude, with Ryan approving UniFi actions
- Controller firmware: `v0.11.0-36-g81829fb` (`esp32-furnace-controller-waveshare`, on Ethernet);
  `1ecd165` from F2, `e38b77a` for the F2 fix checks
- Display firmware: `v0.11.0-36-g81829fb` (`thermostat-bench-normal`); same later builds
- Checklist reference: `docs/hil-checklist.md`
- Design: `docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md`
- Setup notes:
  - Bench controller is the Waveshare board on Ethernet (not PoE-powered), so its WiFi
    radio is used only for ESP-NOW and sits on the configured channel (6). The display's
    AP was also on channel 6 for F1 and F4.
  - Serial logs were not available (no SSH to the bench Pi this session); evidence is
    from `/status`, the controller's audit log and the bench broker.
  - Before F1, the bench controller was found connected to the production broker (no
    `mqtt_host` in NVS, compiled-in default). Fixed in `81829fb`; see `tasks/lessons.md`.

| Test | Result | Notes |
|---|---|---|
| F1 Baseline | PASS (expectation corrected) | Both `espnow_connected` and `espnow_encrypted` true, channel 6, both `connection_path` `mqtt+esp-now`, `failsafe_active` false, both sides' `last_espnow_rx_ms` advancing. `espnow_search_count` was 1 on both, not 0: one search pass runs at boot before the network is up. Checklist now says at most 1. |
| F2 Both lose their network | PASS (partial), found two bugs | Run on `1ecd165`. Controller's switch port disabled (in the UniFi UI) and, 3 min later, the display blocked in UniFi for 9 min. Neither board rebooted (uptime continuous, `net_reboot_count` 0), `failsafe_active` stayed false, the display stayed `espnow_connected` on channel 6 throughout, and both rejoined on their own. No rendezvous was needed: the Ethernet controller's radio already sits on the configured channel, so this did not exercise channel search. Command path: mode heat, setpoint 22.5 and fan on sent to the display back to back while only the controller was cut off. Fan applied over ESP-NOW at once, setpoint was already 22.5, but **mode never reached the controller**. Not reproduced in two later runs, but the cause is a gap in the code: a command frame whose ESP-NOW delivery failed was never re-sent, and the frames after it preserved mode, so the change was lost once the display took back the controller's state. Fixed in `7bfa3e6`: fields not yet reported delivered ride along in every later command and are re-sent on a failed delivery (at most 3 times). The same commit fixes a throttle-merge bug from `f77334f`: a throttled sync merged into a real change voided it, because the controller applies nothing else from a sync word. Checking `7bfa3e6` on the bench found a second, older bug: after a display reboot the controller can reject its commands as stale until the display's seq catches up, up to about 256 commands later. Fixed in `e38b77a` with a sender session in the command word; four display-only reboots in a row each had their first command applied (`command session N->N+1` audited), and the F2 command burst with the port off applied in full. Not covered: commands during the display block itself (the display had no MQTT; nobody touched its screen). |
| F3 Controller loses its network | PASS, found a bug | Ethernet port disabled at the switch for about 8 min (Ryan disabled it in the UI; the API disable needs `forward` and the native network changed together, found later). The display stayed `espnow_connected`, its `last_espnow_rx_ms` advanced throughout, the controller did not reboot (uptime continuous, `net_reboot_count` 0) and `failsafe_active` stayed false; it rejoined MQTT on its own. Command path: mode and setpoint sent to the display over MQTT a few ms apart. The mode reached the controller over ESP-NOW at once; the setpoint was dropped by the display's 50 ms send throttle and applied only when the controller's network returned 7 min later. Fixed in `f77334f` (throttled commands are kept and merged into the next send). |
| F4 Display loses WiFi | PASS (partial) | Display blocked in UniFi for about 4 min. Controller stayed `espnow_connected`, its `last_espnow_rx_ms` advanced every 2-15 s throughout (display frames, encrypted), `failsafe_active` stayed false. After unblock the display was back on WiFi and MQTT within 20 s, with no reboot (uptime continuous) and `net_reboot_count` 0. Not covered: AP channel equal to the controller's channel, so no channel following was exercised (F5 covers that); display commands moving relays not checked (display unreachable while blocked). |

Open observations:
- The Ethernet controller's `espnow_search_count` creeps up while linked (1 at boot, 3
  after 9 min, 4 after 13 min; 71 over an earlier 11 h boot). On Ethernet the policy never
  sees WiFi associated, so any 30 s gap in display frames starts a search pass. Display
  frames arrive every 2-15 s, so a gap means two or more lost frames. Watch whether the
  passes cost link time. It reached 23 by the end of F2 (about 2.5 h of uptime).
- With the controller's port cut off through the UniFi API (which drops traffic but
  leaves the Ethernet link up), display commands reached the controller's audit log
  14-19 s after they were sent, in four runs. Root cause (found with the loop profiler
  added in `870d5d3`): the half-open broker connection still read as connected, each
  publish blocked in the framework's socket write (up to 10 s), and the controller's
  loop publishes under the MQTT mutex, so the whole control loop stalled for up to 60 s
  (relays and failsafe included). Fixed in `4a7df9d` with a 250 ms write budget per MQTT
  packet; the same test then showed a longest loop gap of 365 ms and commands applied in
  the same second.
- UniFi stats lag: `stat/device` reported the port down about 3 min after it stopped
  passing traffic. Use the device's own reachability, not the switch stats, to time tests.
