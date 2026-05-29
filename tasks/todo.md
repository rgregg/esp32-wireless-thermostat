# TODO

## Controller stability: task_wdt resets

### Problem
Controller (and display) report `reset_reason=task_wdt` with `reboot_reason=none`
— hard Task Watchdog panics, *not* the firmware's graceful self-reboots. Root
cause: synchronous blocking network calls run longer than the ~5s TWDT timeout,
starving an idle task. Two paths qualify:
- **Weather HTTPS fetch** (async task on Core 0): `kCtrlHttpTimeoutMs = 8000` (8s) > 5s TWDT.
- **MQTT connect** (inline in `loop()`, Core 1): PubSubClient default 15s socket timeout.

Regression: the async weather task (2026-03-02) and Core-0 web task (2026-03-10)
exposed these blocking calls to the per-core idle-task watchdog after initial
bring-up. Weak WiFi (RSSI −75 dBm) makes the stalls more frequent → intermittent
(not a tight loop; 39+ min healthy uptime observed).

### Decisions (confirmed with user)
- Crash capture: **RTC-memory breadcrumb** (coredump-to-flash infeasible — 4 MB flash
  fully partitioned, partition change needs physical reflash, no serial available).
- Timeouts: **TWDT 15s / HTTP 4s (connect + read) / PubSub 5s** — every app-level
  timeout fires well before the 15s watchdog (defense in depth).
- Scope: controller + display. Both share the PubSubClient busy-spin root cause.
  Display fix differs: it explicitly registers its main+web tasks with the TWDT
  (esp_task_wdt_add/reset) at the default ~5s, so it gets setSocketTimeout(5) +
  a TWDT bump to 15s (idle_core_mask=0, preserving its explicit-task model).

### Tasks
- [ ] Add `#include <esp_task_wdt.h>` to `esp32_controller_main.cpp`.
- [ ] **TWDT → 15s, explicit:** in `setup()`, call `esp_task_wdt_reconfigure()`
      (timeout_ms=15000, idle_core_mask = both cores, trigger_panic=true) to make
      steady-state deterministic regardless of Arduino default.
- [ ] Keep OTA-restore (`ota_web_updater.cpp:250`) in sync: 5000→15000, restore a
      watching idle_core_mask (not 0) so the WDT isn't left disabled after OTA.
- [ ] **HTTP timeout:** `kCtrlHttpTimeoutMs` 8000 → 4000; add
      `http.setConnectTimeout(kCtrlHttpTimeoutMs)` in both weather fetch fns
      (bound TCP/TLS connect + handshake, not just read). Single worst-case op
      ~4s vs 15s TWDT.
- [ ] **PubSub socket timeout:** `g_ctrl_mqtt.setSocketTimeout(5)` once in `setup()`.
- [ ] **RTC breadcrumb:** `RTC_NOINIT_ATTR` magic + breadcrumb word; enum
      {None, WeatherGeocode, WeatherForecast, MqttConnect, Mdns}; set before / clear
      after each blocking call. On boot: if magic valid capture last breadcrumb,
      else init (power-on). Reset breadcrumb for new session.
- [ ] Surface breadcrumb: publish retained `state/wdt_section` next to `reset_reason`;
      add a HA diagnostic discovery sensor "Controller WDT Section"; log to audit on boot.
- [ ] `pio run -e native-tests` green (firmware files excluded; sanity check no breakage).
- [ ] `pio run -e esp32-furnace-controller` green.
- [ ] OTA-deploy to 192.168.42.57; confirm `state/wdt_section` publishes; monitor for
      reduced `task_wdt` and read the breadcrumb if a reset occurs.

### Notes / tradeoffs
- TWDT trips on contiguous *non-yielding CPU* (mainly the TLS handshake), not on
  socket waits (those block-and-yield, letting idle feed the WDT). So the binding
  constraint is each HTTP phase's timeout < TWDT. At 4s connect + 4s read, the
  worst single op is ~4s vs 15s — comfortable margin, and a truly wedged fetch
  still produces a fast `task_wdt` reset with a "weather_*" breadcrumb (the
  weather-wedge graceful watchdog is ~30 min, far slower).
- RTC_NOINIT survives watchdog/panic/SW resets, not power-on/brownout — handled by magic check.
- Breadcrumb is per-core (array indexed by xPortGetCoreID), eliminating the
  cross-core race so the recovered section is unambiguous about which core stalled.
- Confirmed root cause via PubSubClient.cpp:257 — `connect()` busy-spins on
  `while(!available())` with no yield for up to socketTimeout; default 15s → 15s
  core-1 CPU starvation → trips old 5s TWDT. socketTimeout→5s + TWDT→15s fixes it.

### Review
All changes scoped to the controller; no shared headers touched.

**`src/esp32_controller_main.cpp`**
- `#include <esp_task_wdt.h>`.
- `kCtrlHttpTimeoutMs` 8000 → 4000; added `http.setConnectTimeout(kCtrlHttpTimeoutMs)`
  to both weather fetch fns (bounds TCP+TLS handshake, not just read).
- New constants `kCtrlTaskWdtTimeoutMs = 15000` and `kCtrlMqttSocketTimeoutS = 5`.
- `setup()`: explicit `esp_task_wdt_reconfigure()` (15s, idle_core_mask=0x3 both
  cores, trigger_panic) + `g_ctrl_mqtt.setSocketTimeout(5)`.
- RTC breadcrumb: `RTC_NOINIT` magic + **per-core** section array (indexed by
  `xPortGetCoreID()`, so weather/core-0 and MQTT/core-1 never race); set/clear
  around weather geocode, weather forecast, MQTT connect, mDNS;
  `ctrl_breadcrumb_recover_on_boot()` reports `core0=… core1=…` (or "none").
- Surfacing: retained `state/wdt_section`, HA discovery sensor "Controller WDT
  Section", and a boot audit line when the captured section != "none".

**`src/ota_web_updater.cpp`**
- Post-OTA WDT restore 5000/mask=0 → 15000/mask=0x3 (was leaving the watchdog
  disabled until the next reboot). Kept in sync with the controller steady-state.

**Verification**
- `pio run -e native-tests` + `./.pio/build/native-tests/program` → 151/151 pass.
- `pio run -e esp32-furnace-controller` → SUCCESS, firmware.bin produced.

**Deploy (pending user)**
- OTA to 192.168.42.57. Per runbook: set HVAC Off + wait 60s before flashing.
- After boot: confirm `state/wdt_section` publishes ("none" on a clean boot).
- If a `task_wdt` recurs, the breadcrumb names the culprit (weather_* / mqtt_connect / mdns).

**Display (`src/thermostat/esp32s3_thermostat_firmware.cpp`)**
- Same PubSubClient busy-spin in `ensure_mqtt_connected()` (line 1833), runs in the
  WDT-registered main task → 15s spin starves `esp_task_wdt_reset()` → task_wdt.
- Fix: `g_mqtt.setSocketTimeout(5)` + `esp_task_wdt_reconfigure()` to 15s
  (idle_core_mask=0, keeps explicit main+web task watching). No breadcrumb (its
  explicit task registration already attributes a panic to the main task; 16MB
  flash likely has a coredump partition — possible future avenue).
- `pio run -e esp32-furnace-thermostat` → SUCCESS, firmware.bin produced.
