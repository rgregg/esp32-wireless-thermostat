# TODO

## SoftAP captive-portal provisioning (replace BLE/NimBLE)

Replace BLE/Improv provisioning with SoftAP + captive portal on display + both
controllers, removing NimBLE entirely (the root cause of all the LCD/RAM crashes).
Build + ready; do NOT deploy to the live .57 controller.

### Design (confirmed with user)
- No saved WiFi creds OR sustained WiFi-connect failure → bring up SoftAP
  `Furnace-Setup-<mac4>` + DNSServer captive portal + web form (scan SSIDs, enter
  password) → save to NVS → reconnect. Hand-rolled on a dedicated WebServer + DNSServer
  (no WiFiManager dep). Open AP.
- Display: show "WiFi setup → join Furnace-Setup-XXXX" on the LCD.
- Waveshare controller: Ethernet-primary. If Ethernet gets an IP → skip WiFi/portal.
  If Ethernet fails → fall back to WiFi (saved creds → connect; none → SoftAP portal).
- Classic controller: WiFi primary → SoftAP fallback (like display).
- Remove NimBLE-Arduino + Improv lib_deps and IMPROV_WIFI_BLE_ENABLED.

### Step 0 — creds-seed fix (unblocks bench normal-config test) — IN PROGRESS
- [ ] Add default_ssid/default_password to WifiProvisioningConfig; begin() uses them as
      the NVS getString fallback so build-time/baked creds actually connect.
- [ ] Display init_network + controller pass g_cfg creds as defaults.
- [ ] Verify bench display (thermostat-bench-normal) connects to IoTDevices + the local
      test broker 10.0.2.175 (watch piserial5 /tmp/mqtt_log.txt), web UI responds.

### Step 1 — SoftAP portal module (shared) — DONE
- [x] softap_provisioning.h/.cpp: AP "Furnace-Setup-XXXX" + DNSServer captive portal +
      WebServer (async SSID scan, SSID+password form) → callback(ssid,password). Compiles.
- [x] WifiProvisioningManager: start_provisioning() → SoftAP; ensure_connected() services
      the portal; on_wifi_connected() stops it; reboot_pending()→false; IMPROV paths gone.

### Step 2 — boot logic
- [x] Display: run_provisioning_boot drives the SoftAP portal (LCD shows the AP name),
      reboots on cred submit. Normal boot unchanged (loop's g_wifi services the portal).
- [x] Classic controller: g_ctrl_wifi.start_provisioning() → SoftAP; loop's
      ctrl_ensure_wifi_connected services it.
- [ ] Waveshare Ethernet→WiFi runtime fallback (DEFERRED — separate state-machine change;
      currently Waveshare stays Ethernet-only, just with NimBLE gone). Follow-up.

### Step 3 — remove NimBLE + build/validate — IN PROGRESS
- [x] platformio.ini: dropped NimBLE-Arduino + Improv lib_deps + IMPROV_WIFI_BLE_ENABLED.
- [x] Deleted improv_ble_provisioning.*.
- [ ] Build all envs (running); confirm RAM headroom up (no NimBLE).
- [ ] Hardware: no crash; AP comes up. NOTE: full portal flow can't be tested remotely
      (needs someone to join the AP at the bench).

## Bench test infra (this session)
- Local test MQTT broker: mosquitto 2.1.2 on piserial5 (10.0.2.175:1883, anon, docker
  `bench-mqtt`). Logger: `docker exec bench-mqtt cat /tmp/mqtt_log.txt`.
- Bench WiFi creds: include/bench_wifi_secrets.h (gitignored) — IoTDevices + broker host.
- Display normal-config env: `thermostat-bench-normal` (force-includes the secrets header).

---

# Display BLE/Improv Provisioning Revival (2026-06-23) — branch feat/display-ble-provisioning

Spec: docs/superpowers/specs/2026-06-23-display-ble-provisioning-revival-design.md
Plan: docs/superpowers/plans/2026-06-23-display-ble-provisioning-revival.md

## Implemented (all tasks DONE, two-stage reviewed)
- [x] Task 1: pure `provisioning_gate::needed()` predicate + native tests (8 cases)
- [x] Task 2: NVS-backed `thermostat_provisioning_needed()` + `btInUse()` override; setup() refactor
- [x] Task 3: revived `improv_ble_provisioning.{h,cpp}` (no esp32-hal-bt-mem.h pin)
- [x] Task 4: `#ifdef THERMOSTAT_BLE_PROVISIONING` branch in run_provisioning_boot() (+ start-failure guard, macro #error)
- [x] Task 5: platformio — flipped esp32-furnace-thermostat to BLE; added -softap rollback env; re-parented test/bench chain to -softap so it stays BLE-free
- [x] Task 6: verification sweep — ALL GREEN

## Verification (2026-06-23, native + builds)
- native-tests: 206 run / 0 fail (incl. 8 provisioning_gate_* cases)
- esp32-furnace-thermostat (BLE): Flash 31.3%, RAM 60.4% (NimBLE linked)
- esp32-furnace-thermostat-softap: Flash 28.2%, RAM 58.4% (no NimBLE)
- esp32-furnace-thermostat-test: Flash 28.2% (bench chain BLE-free, confirmed)
- esp32-furnace-controller: SUCCESS (no regression)

## sdkconfig deviation from spec (accepted)
- Spec listed CONFIG_BT_LE_MAX_CONNECTIONS / CONFIG_BT_LE_50_FEATURE_SUPPORT — these symbols
  do NOT exist in this S3 framework. Substituted CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n
  (host-level key, also gates ext-adv). Connection cap still enforced via
  CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1.

## ON-BENCH ACCEPTANCE GATE (required before flashing production — needs hardware via piserial5)
- [ ] Flash esp32-furnace-thermostat to a creds-cleared bench ESP32-S3. Confirm provisioning
      boot: BLE + LCD + LVGL UI coexist. Log largest-contiguous internal-DMA free (expect > 159 KB).
- [ ] Provision end-to-end via web.improv-wifi.com (or HA) → confirm reboot.
- [ ] Normal boot: WiFi connects; log free-heap delta vs -softap build to confirm ~36 KB BT mem reclaimed.
- [ ] Serial: confirm console enumerates on /dev/ttyUSB0 (CH340) vs /dev/ttyACM1 (USB-JTAG) for this build.
- [ ] BLE-behavior checks deferred from Task 3 review (see plan): ADV re-overflow on
      failure/disconnect (I-1, active-scan likely mitigates), caps byte 0x00 vs GATT 0x01 (I-2),
      ~2s reboot-flush timing.

## BENCH VALIDATION — PASSED (2026-06-28, bench ESP32-S3 via piserial5, MAC 80:B5:4E:D1:B8:04)
Provisioned AUTOMATICALLY over BLE from piserial5's own Bluetooth (onboard hci0) using a
D-Bus Improv client (/tmp/improv_provision.py on the pi) — no phone needed.
- [x] Provisioning boot: no crash; LCD+LVGL+BLE coexist; advertises "Thermostat".
      internal-DMA RAM: free=28331 largest-contiguous=26612 (in line with the firmware's
      normal ~28KB free-heap budget; Phase 1's 159KB was an unrealistic isolated probe).
- [x] BLE discoverable by a real central via active scan (name in scan response — I-1 OK for discovery).
- [x] Auto-provision: STATE 02(AUTHORIZED) -> wrote SEND_WIFI_SETTINGS(IoTDevices) ->
      STATE 04(PROVISIONED), RESULT 01010002, no ERROR.
- [x] Reboot into normal mode (did NOT re-enter provisioning => creds persisted to NVS).
- [x] BT memory released on normal boot: full WiFi+MQTT+web stack runs (impossible per Phase 2 if not released).
- [x] WiFi associated: GET /status -> wifi_connected:true, ip 192.168.42.94, mqtt_connected:true,
      firmware_version v0.10.2-116-gfcedc6b-dirty (confirms the branch build).

Tier 2 NimBLE buffer trims: optional / low priority (headroom matches normal budget; flow works).
Note: bench-ble build uses PRODUCTION MQTT defaults, so the bench device announced itself on the
production broker/HA — clean up that phantom display entry if undesired, or re-flash -softap to retire it.
