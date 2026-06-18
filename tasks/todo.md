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
