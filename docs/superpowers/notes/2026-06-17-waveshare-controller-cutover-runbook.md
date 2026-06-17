# Waveshare ESP32-S3 Controller — Cutover Runbook

How to replace the live furnace controller (classic ESP32, `192.168.42.57`, MAC
`38:18:2B:EC:FB:9C`) with the Waveshare ESP32-S3 board, with **zero Home Assistant or
display reconfiguration** thanks to the identity-MAC-override feature.

> **Status of the port (2026-06-17):** all firmware is built + bench-validated on the real
> Waveshare board (relays, RTC, Ethernet, coredump, resilience, identity override). The ONE
> thing not yet validated is the controller↔display ESP-NOW exchange end-to-end (no spare
> display on the bench) — validate that first on the bench with the real display if possible,
> or accept it as the one live-test risk during cutover (ESP-NOW *init* is proven; the dropin
> MAC + channel + LMK below are what make the existing display peer work unchanged).

---

## What's config vs. what's build-time

| Cutover parameter | How to set | Production value |
|---|---|---|
| Identity MAC (HA + ESP-NOW) | NVS `id_mac` / web "Identity MAC Override" | `38:18:2B:EC:FB:9C` |
| Base topic | NVS `base_topic` (or the prod env default) | `esp32-wireless-thermostat` |
| MQTT broker | NVS `mqtt_host` | `mqtt.lan` |
| HA discovery prefix | NVS `discovery_prefix` | `homeassistant` |
| ESP-NOW channel | NVS `espnow_channel` (or prod env default) | `6` |
| ESP-NOW LMK | NVS `espnow_lmk` (or prod env default) | `a1b2c3d4e5f60718293a4b5c6d7e8f90` |
| **Relay → HVAC mapping** | **BUILD-TIME** (`Pca9554RelayBackendConfig`) | default heat=relay0, cool=relay1, fan=relay2 |

**Recommended firmware:** build `esp32-furnace-controller-waveshare-prod`. It already bakes in
the production base topic + ESP-NOW channel 6 + production LMK (the same defaults the classic
controller uses, so the existing display peers to it). You then only need to set **`id_mac`**
(and `mqtt_host` if not `mqtt.lan`) via the web UI. The `-waveshare` (test-identity) env is for
bench work only and must NOT be used for cutover.

**Relay mapping is the one build-time item.** Wire the furnace so W (heat)→relay0,
Y (cool)→relay1, G (fan)→relay2 to match the default. If your existing wiring differs, edit the
`Pca9554RelayBackend g_relay_backend` construction (`heat_bit/cool_bit/fan_bit`) and rebuild.

---

## Pre-cutover checklist
- [ ] (Recommended) Validate controller↔display ESP-NOW on the bench with a real display.
- [ ] Confirm the relay→furnace wiring plan (default heat0/cool1/fan2, or rebuild for a custom map).
- [ ] Decide Ethernet addressing: DHCP (works) or a **DHCP reservation** for the board's
      Ethernet MAC (recommended for a stable IP / OTA URL). Note the Ethernet MAC is derived
      from the override base MAC once `id_mac` is set (it is `id_mac + 3`), so set the
      reservation after first boot when you can read the leased MAC.
- [ ] (Optional but recommended) Populate the RTC backup battery so time survives a cold boot.
- [ ] Build `esp32-furnace-controller-waveshare-prod` and stage the 4 flash images.

## Cutover procedure

1. **Quiesce the old controller.** In HA (or via MQTT) set HVAC **Off**, wait 60 s for relays
   to de-energize (the flashing runbook). Then power it down / unplug it.

2. **Flash the Waveshare board** with `esp32-furnace-controller-waveshare-prod`:
   ```
   esptool --chip esp32s3 --port <port> --after hard_reset write_flash \
     0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
   ```
   On first boot it comes up on Ethernet (DHCP) with its own factory MAC, base topic
   `esp32-wireless-thermostat`, ESP-NOW channel 6.

3. **Set the identity override.** Browse to the board's web UI (`http://<eth-ip>/`, the IP is on
   the serial boot log / your DHCP server) → Config → MQTT → **Identity MAC Override** =
   `38:18:2B:EC:FB:9C`. Set `mqtt_host` if not `mqtt.lan`. Save → reboot.
   - After reboot the serial log shows `[espnow] radio MAC=38:18:2B:EC:FB:9C identity MAC=
     38:18:2B:EC:FB:9C` and HA sees the **same** controller device (same uniq_id/topics) — no
     new entities, no display re-pairing.

4. **Wire the furnace** to the relays (W→relay0, Y→relay1, G→relay2) and power up. (Do this
   with the HVAC system safe / breaker off as appropriate.)

## Verification
- [ ] Serial/`[espnow]` log confirms radio + identity MAC = `38:18:2B:EC:FB:9C`.
- [ ] HA shows the existing controller device online (no duplicate/new device); `state/*` update.
- [ ] The thermostat **display** controls the controller over ESP-NOW (change mode/setpoint on
      the display → controller `state/mode` etc. follow; `espnow_connected`/heartbeat healthy).
- [ ] Relays actuate correctly: a heat call closes relay0, cool→relay1, fan→relay2; the
      interlock forces off+settle when switching heat↔cool.
- [ ] Ethernet link stable; MQTT connected; `state/time_utc` sane (RTC/SNTP).
- [ ] Trigger nothing, but note `state/reboot_reason`/`panic_pc` for clean-boot diagnostics.

## Rollback
If anything is wrong: set HVAC Off, power down the Waveshare board, re-wire + power up the old
classic controller. Because the Waveshare assumed the old MAC/identity, HA + the display return
to the old controller with no reconfiguration. (Clear the Waveshare's retained MQTT messages if
it published bad state: empty-retained-publish to its `state/*` + the HA discovery configs.)

## Notes / residual risk
- The controller↔display ESP-NOW path is the only un-bench-validated function; the dropin MAC +
  channel 6 + production LMK are specifically what make the existing display work without
  changes, but confirm on first live boot.
- Two controllers must never run with `id_mac` = the same MAC simultaneously (MAC conflict).
  Always power down the old one first.
- The pre-existing ESP-NOW-recv-mutates-app() data race (benign in practice) is unchanged; a
  follow-up could route ESP-NOW commands through the same inbound queue used for MQTT.
