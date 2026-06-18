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

### Step 1 — SoftAP portal module (shared)
- [ ] New softap_provisioning.h/.cpp: start AP + DNSServer + portal WebServer; SSID scan
      page; on submit -> callback(ssid, password). stop() tears it down.
- [ ] WifiProvisioningManager: start_provisioning() -> SoftAP portal (not BLE);
      on_wifi_connected()/reboot_pending() updated; drop IMPROV paths.

### Step 2 — boot logic
- [ ] Display: drop run_provisioning_boot BLE-RAM special case; normal boot → WiFi →
      SoftAP fallback. LCD shows AP name when portal active.
- [ ] Controller: Ethernet-primary (Waveshare) → WiFi fallback → SoftAP; classic → WiFi
      → SoftAP.

### Step 3 — remove NimBLE + build/validate
- [ ] platformio.ini: drop NimBLE-Arduino + Improv lib_deps + IMPROV_WIFI_BLE_ENABLED.
- [ ] Delete improv_ble_provisioning.*.
- [ ] Build all envs; confirm internal RAM headroom up (no NimBLE).
- [ ] Hardware: no crash; AP comes up. NOTE: full portal flow can't be tested remotely
      (needs someone to join the AP at the bench).

## Bench test infra (this session)
- Local test MQTT broker: mosquitto 2.1.2 on piserial5 (10.0.2.175:1883, anon, docker
  `bench-mqtt`). Logger: `docker exec bench-mqtt cat /tmp/mqtt_log.txt`.
- Bench WiFi creds: include/bench_wifi_secrets.h (gitignored) — IoTDevices + broker host.
- Display normal-config env: `thermostat-bench-normal` (force-includes the secrets header).
