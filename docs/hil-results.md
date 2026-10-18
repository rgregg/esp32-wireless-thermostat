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
- Controller firmware: `v0.11.0-36-g81829fb` (`esp32-furnace-controller-waveshare`, on Ethernet)
- Display firmware: `v0.11.0-36-g81829fb` (`thermostat-bench-normal`)
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
| F3 Controller loses its network | PASS, found a bug | Ethernet port disabled at the switch for about 8 min (UniFi's API port disable had no effect on this switch; Ryan disabled it). The display stayed `espnow_connected`, its `last_espnow_rx_ms` advanced throughout, the controller did not reboot (uptime continuous, `net_reboot_count` 0) and `failsafe_active` stayed false; it rejoined MQTT on its own. Command path: mode and setpoint sent to the display over MQTT a few ms apart. The mode reached the controller over ESP-NOW at once; the setpoint was dropped by the display's 50 ms send throttle and applied only when the controller's network returned 7 min later. Fixed in `f77334f` (throttled commands are kept and merged into the next send). |
| F4 Display loses WiFi | PASS (partial) | Display blocked in UniFi for about 4 min. Controller stayed `espnow_connected`, its `last_espnow_rx_ms` advanced every 2-15 s throughout (display frames, encrypted), `failsafe_active` stayed false. After unblock the display was back on WiFi and MQTT within 20 s, with no reboot (uptime continuous) and `net_reboot_count` 0. Not covered: AP channel equal to the controller's channel, so no channel following was exercised (F5 covers that); display commands moving relays not checked (display unreachable while blocked). |

Open observations:
- The Ethernet controller's `espnow_search_count` creeps up while linked (1 at boot, 3
  after 9 min, 4 after 13 min; 71 over an earlier 11 h boot). On Ethernet the policy never
  sees WiFi associated, so any 30 s gap in display frames starts a search pass. Display
  frames arrive every 2-15 s, so a gap means two or more lost frames. Watch whether the
  passes cost link time.
