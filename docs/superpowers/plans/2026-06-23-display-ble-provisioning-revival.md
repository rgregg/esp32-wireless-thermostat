# Display BLE/Improv Provisioning Revival — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-enable BLE/Improv WiFi provisioning on the display as a compile-time alternative to the SoftAP captive portal, gated to a WiFi-down provisioning boot, with BT controller memory released on every normal boot (zero runtime cost when provisioned).

**Architecture:** A single provisioning-entry predicate (`provisioning_gate::needed`, a pure header-only function) is consumed by both a strong `btInUse()` override (which runs inside `initArduino()` after `nvs_flash_init()` and decides whether the Arduino core releases BT controller memory) and by `thermostat_firmware_setup()` (which decides whether to enter `run_provisioning_boot()`). The provisioning boot branches at compile time between the revived `improv_ble_provisioning` module and today's SoftAP path. NimBLE is trimmed to the minimum an unpaired one-shot Improv peripheral needs.

**Tech Stack:** ESP32-S3, Arduino-as-IDF-component (pioarduino), NimBLE-Arduino, jnthas Improv WiFi Library, LVGL, ESP-IDF NVS C API. Native unit tests via the in-repo `TEST_CASE`/`ASSERT_*` harness (`-DTHERMOSTAT_RUN_TESTS`).

**Spec:** `docs/superpowers/specs/2026-06-23-display-ble-provisioning-revival-design.md`

**Plan refinement vs. spec §3:** the spec named `btInUse()` calling `thermostat_provisioning_needed()`. This plan keeps that, but factors the *logic* into a platform-agnostic pure helper (`provisioning_gate::needed`) so it is unit-testable on native (per CLAUDE.md code-sharing rules); the Arduino wrapper only does NVS I/O. The BLE on-connected callback persists creds via the existing `WifiProvisioningManager::set_credentials()` (a pure NVS write — it does **not** bring up WiFi STA), exactly as the pre-removal code did, so WiFi never comes up during the BLE boot.

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `include/provisioning_gate.h` | Pure, platform-agnostic provisioning-entry predicate | Create |
| `src/tests/test_provisioning_gate.cpp` | Native unit tests for the predicate | Create |
| `include/improv_ble_provisioning.h` | BLE/Improv provisioning module interface | Create (revive from history) |
| `src/improv_ble_provisioning.cpp` | NimBLE + Improv GATT, reboot-trick, ADV fix | Create (revive from history, minus BT-mem pin) |
| `src/thermostat/esp32s3_thermostat_firmware.cpp` | `thermostat_provisioning_needed()` wrapper, `btInUse()` override, `run_provisioning_boot()` BLE branch, `setup()` refactor | Modify |
| `platformio.ini` | Flip `esp32-furnace-thermostat` to BLE; add `esp32-furnace-thermostat-softap` | Modify |

**Two compile-time flags (always set together in the BLE env):**
- `IMPROV_WIFI_BLE_ENABLED` — compiles the Improv module + the `btInUse()` override; pulls NimBLE.
- `THERMOSTAT_BLE_PROVISIONING` — selects the BLE branch in `run_provisioning_boot()`.

**NVS facts (namespace `cfg_disp`):** SSID key `wifi_ssid` (Arduino `Preferences::putString` → `nvs_set_str`); WiFi-disabled key `wifi_off` (`Preferences::putBool` → `nvs_set_u8`, 1/0). Compile-time defaults: `THERMOSTAT_WIFI_SSID` (default `""`), `THERMOSTAT_WIFI_DISABLED` (default `0`).

---

## Task 1: Pure provisioning-entry predicate + native tests

**Files:**
- Create: `include/provisioning_gate.h`
- Test: `src/tests/test_provisioning_gate.cpp`

- [ ] **Step 1: Write the failing test**

Create `src/tests/test_provisioning_gate.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include "provisioning_gate.h"
#include "test_harness.h"

// Fresh device: WiFi enabled, no stored SSID, no baked default -> provisioning needed.
TEST_CASE(provisioning_gate_fresh_device) {
  ASSERT_TRUE(provisioning_gate::needed(false, "", ""));
}

// Stored SSID present -> not needed (NVS creds win).
TEST_CASE(provisioning_gate_stored_ssid) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "MyNet", ""));
}

// Baked default SSID suppresses provisioning even with empty NVS.
TEST_CASE(provisioning_gate_baked_default) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "", "BakedNet"));
}

// NVS SSID wins over baked default; still not needed.
TEST_CASE(provisioning_gate_nvs_over_baked) {
  ASSERT_TRUE(!provisioning_gate::needed(false, "MyNet", "BakedNet"));
}

// ESP-NOW-only mode (WiFi disabled): never provision, regardless of SSID state.
TEST_CASE(provisioning_gate_wifi_disabled) {
  ASSERT_TRUE(!provisioning_gate::needed(true, "", ""));
}

// Null NVS pointer (key absent) behaves like empty string.
TEST_CASE(provisioning_gate_null_nvs_ssid) {
  ASSERT_TRUE(provisioning_gate::needed(false, nullptr, ""));
  ASSERT_TRUE(!provisioning_gate::needed(false, nullptr, "BakedNet"));
}

#endif
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio run -e native-tests`
Expected: FAIL — compile error, `provisioning_gate.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `include/provisioning_gate.h`:

```cpp
#pragma once

// Platform-agnostic provisioning-entry predicate. Single source of truth shared by:
//   - the Arduino btInUse() override (decides BT controller memory release in initArduino)
//   - thermostat_firmware_setup() (decides whether to enter run_provisioning_boot)
// One predicate prevents the two decisions from drifting: a drift would either waste BT
// RAM on a normal boot or starve the provisioning boot of the RAM BLE needs.

namespace provisioning_gate {

// Returns true when the device should enter WiFi provisioning.
//   wifi_disabled : ESP-NOW-only mode never provisions (no SSID is needed).
//   nvs_ssid      : SSID stored in NVS, or nullptr/"" if absent.
//   baked_ssid    : compile-time baked default SSID, or "" if none.
// The effective SSID is the NVS value if non-empty, else the baked default.
// Provisioning is needed iff WiFi is enabled AND no effective SSID exists.
inline bool needed(bool wifi_disabled, const char *nvs_ssid, const char *baked_ssid) {
  if (wifi_disabled) return false;
  const bool nvs_has = (nvs_ssid != nullptr && nvs_ssid[0] != '\0');
  const char *eff = nvs_has ? nvs_ssid : baked_ssid;
  return (eff == nullptr || eff[0] == '\0');
}

}  // namespace provisioning_gate
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: build succeeds; runner prints all tests passing including the six `provisioning_gate_*` cases; exit code 0.

- [ ] **Step 5: Commit**

```bash
git add include/provisioning_gate.h src/tests/test_provisioning_gate.cpp
git commit -m "feat(provision): pure provisioning-entry predicate + native tests

Single source of truth for 'should this boot provision?', shared (next
commits) by the btInUse() BT-mem gate and setup(). Header-only and
platform-agnostic per CLAUDE.md code-sharing rules.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Arduino NVS wrapper + `btInUse()` override + `setup()` refactor

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp` (add wrapper + override near top; refactor the provisioning check in `thermostat_firmware_setup()` ~line 2811)

This task has no native test (it does ESP NVS I/O behind `#ifdef ARDUINO`). Verification is a clean BLE-env build in Task 5; the *logic* is already covered by Task 1.

- [ ] **Step 1: Add the include near the other includes**

Find the `#include "wifi_provisioning_manager.h"` line (~line 17) and add directly after it:

```cpp
#include "provisioning_gate.h"
#ifdef ARDUINO
#include <nvs.h>
#endif
```

- [ ] **Step 2: Add the NVS wrapper + btInUse() override**

Add this block immediately before the `// ---------- SoftAP provisioning boot mode ----------` comment (~line 2714). It is placed in this translation unit because the firmware file is only compiled for display envs, and `btInUse()` must have external C linkage to override the core's weak symbol.

```cpp
#ifdef ARDUINO
// Reads NVS directly via the C API so it is safe to call from initArduino()/btInUse(),
// before the Arduino Preferences object (g_cfg) is opened. Mirrors the provisioning-entry
// condition in thermostat_firmware_setup() exactly (see provisioning_gate::needed).
static bool thermostat_provisioning_needed() {
  bool wifi_disabled = (THERMOSTAT_WIFI_DISABLED != 0);
  char ssid[64] = {0};
  nvs_handle_t h;
  // NVS_READONLY: namespace is absent on a truly fresh device -> open fails -> we keep the
  // compile-time defaults (empty SSID, not disabled) -> provisioning needed. Fail-safe:
  // if NVS is unreadable we retain BT memory (a wasted ~36 KB on a normal boot is
  // recoverable; starving the provisioning boot of BT memory is not).
  if (nvs_open("cfg_disp", NVS_READONLY, &h) == ESP_OK) {
    uint8_t off = 0;
    if (nvs_get_u8(h, "wifi_off", &off) == ESP_OK) {
      wifi_disabled = (off != 0);
    }
    size_t len = sizeof(ssid);
    if (nvs_get_str(h, "wifi_ssid", ssid, &len) != ESP_OK) {
      ssid[0] = '\0';
    }
    nvs_close(h);
  }
  return provisioning_gate::needed(wifi_disabled, ssid, THERMOSTAT_WIFI_SSID);
}

#ifdef IMPROV_WIFI_BLE_ENABLED
// Strong override of the Arduino core's weak btInUse() (esp32-hal-bt.c). Runs in
// initArduino() after nvs_flash_init() and before esp_bt_controller_mem_release().
// Provisioning boot (no creds) -> keep BT memory so NimBLE can start. Normal boot
// (creds present) -> release ~36 KB. This replaces the old esp32-hal-bt-mem.h, which
// pinned BT memory on every boot.
extern "C" bool btInUse(void) {
  return thermostat_provisioning_needed();
}
#endif  // IMPROV_WIFI_BLE_ENABLED
#endif  // ARDUINO
```

- [ ] **Step 3: Refactor `setup()` to use the wrapper**

In `thermostat_firmware_setup()` find (~line 2811):

```cpp
  if (!g_cfg_wifi_disabled && g_cfg_wifi_ssid.isEmpty()) {
    run_provisioning_boot();
    return;  // never reached — run_provisioning_boot loops forever
  }
```

Replace the condition with the shared wrapper so it can never diverge from `btInUse()`:

```cpp
  if (thermostat_provisioning_needed()) {
    run_provisioning_boot();
    return;  // never reached — run_provisioning_boot loops forever
  }
```

- [ ] **Step 4: Verify the SoftAP build still compiles (no BLE flags yet)**

Run: `pio run -e esp32-furnace-thermostat`
Expected: SUCCESS. At this point the env has no BLE flags, so `btInUse()` is compiled out and `thermostat_provisioning_needed()` is the only new symbol exercised (by `setup()`). This proves the refactor is sound before BLE is introduced.

> Note: `nvs_get_u8` reads what `Preferences::putBool` wrote (it stores a `uint8_t`); `nvs_get_str` reads what `putString` wrote. If a future build changes the storage type for `wifi_off`, update this wrapper to match.

- [ ] **Step 5: Commit**

```bash
git add src/thermostat/esp32s3_thermostat_firmware.cpp
git commit -m "feat(provision): NVS-backed provisioning gate + btInUse() override

setup() now uses thermostat_provisioning_needed() (shared logic with the
btInUse() BT-mem gate, BLE build only). btInUse() keeps BT controller
memory only when provisioning is needed; normal boots release ~36 KB.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Revive the `improv_ble_provisioning` module

**Files:**
- Create: `include/improv_ble_provisioning.h`
- Create: `src/improv_ble_provisioning.cpp`

Restore the pre-removal module (`f44767a^`) with one change: drop `#include "esp32-hal-bt-mem.h"` (that header pinned BT memory on every boot; replaced by the `btInUse()` override from Task 2). Keep the reboot-trick and the ADV/scan-response packet fix.

- [ ] **Step 1: Create the header**

Create `include/improv_ble_provisioning.h`:

```cpp
#pragma once
#ifdef IMPROV_WIFI_BLE_ENABLED

struct ImprovBleConfig {
    const char *device_name;       // BLE advertised name
    const char *firmware_name;     // THERMOSTAT_PROJECT_NAME
    const char *firmware_version;  // THERMOSTAT_FIRMWARE_VERSION
    const char *hardware_variant;  // "ESP32-S3" or "ESP32"
    const char *device_url;        // "http://{LOCAL_IPV4}/" or nullptr
    bool reboot_after_provision;   // true = reboot after saving credentials
};

typedef void (*ImprovBleConnectedCb)(const char *ssid, const char *password);

bool improv_ble_start(const ImprovBleConfig &config, ImprovBleConnectedCb on_connected);
void improv_ble_stop();
bool improv_ble_is_active();
bool improv_ble_reboot_pending();

#endif
```

- [ ] **Step 2: Create the implementation**

Create `src/improv_ble_provisioning.cpp` (revived from history, BT-mem pin removed):

```cpp
#if defined(ARDUINO) && defined(IMPROV_WIFI_BLE_ENABLED)

#include "improv_ble_provisioning.h"
#include <ImprovWiFiBLE.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>
#include "esp_heap_caps.h"

static ImprovWiFiBLE s_improv_ble;
static bool s_active = false;
static bool s_reboot_after = false;
static bool s_reboot_pending = false;
static uint32_t s_reboot_at_ms = 0;
static ImprovBleConnectedCb s_on_connected = nullptr;

static ImprovTypes::ChipFamily chip_family_from_variant(const char *variant) {
    if (variant && strcmp(variant, "ESP32-S3") == 0)
        return ImprovTypes::CF_ESP32_S3;
    if (variant && strcmp(variant, "ESP32-C3") == 0)
        return ImprovTypes::CF_ESP32_C3;
    return ImprovTypes::CF_ESP32;
}

bool improv_ble_start(const ImprovBleConfig &config, ImprovBleConnectedCb on_connected) {
    if (s_active) return false;

    s_on_connected = on_connected;
    s_reboot_after = config.reboot_after_provision;

    s_improv_ble.onImprovConnected([](const char *ssid, const char *password) {
        Serial.printf("[Improv] Credentials accepted for: %s\n", ssid);
        if (s_on_connected) {
            s_on_connected(ssid, password);
        }
        if (s_reboot_after) {
            // Schedule reboot — don't reboot here because the library still needs to
            // send STATE_PROVISIONED and the device URL response after this returns.
            Serial.println("[Improv] Will reboot in 2s after BLE response completes...");
            s_reboot_pending = true;
            s_reboot_at_ms = millis() + 2000;
        }
    });

    s_improv_ble.onImprovError([](ImprovTypes::Error err) {
        Serial.printf("[Improv] Error: %d\n", (int)err);
    });

    // WiFi + BLE cannot coexist on this device (not enough internal RAM). Instead of
    // connecting, persist the credentials (via on_connected) and reboot; on next boot the
    // firmware connects via the saved credentials without starting BLE.
    s_improv_ble.setCustomConnectWiFi([](const char *ssid, const char *password) -> bool {
        Serial.printf("[Improv] Received credentials for SSID: %s\n", ssid);
        return true;  // tell the library "connected"; we reboot momentarily
    });

    ImprovTypes::ChipFamily family = chip_family_from_variant(config.hardware_variant);

    if (config.device_url) {
        s_improv_ble.setDeviceInfo(family, config.firmware_name,
                                   config.firmware_version, config.device_name,
                                   config.device_url);
    } else {
        s_improv_ble.setDeviceInfo(family, config.firmware_name,
                                   config.firmware_version, config.device_name);
    }

    // The library's buildAdvData includes a short name that pushes the primary ADV packet
    // over 31 bytes, causing NimBLE to silently drop the service data. Rebuild with only
    // flags + service data in the primary ADV, and move the UUID + name to the scan
    // response.
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();

    NimBLEAdvertisementData advData;       // primary ADV: flags (3) + service data (12)
    advData.setFlags(0x06);
    uint8_t svc_data[8] = {0x77, 0x46, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    advData.setServiceData(NimBLEUUID((uint16_t)0x4677), svc_data, sizeof(svc_data));
    adv->setAdvertisementData(advData);

    NimBLEAdvertisementData scanResp;      // scan response: UUID (18) + name
    scanResp.addServiceUUID(NimBLEUUID("00467768-6228-2272-4663-277478268000"));
    scanResp.setName(config.device_name);
    adv->setScanResponseData(scanResp);

    adv->start();

    s_active = true;
    Serial.printf("[Improv] BLE advertising as \"%s\"\n", config.device_name);
    return true;
}

void improv_ble_stop() {
    if (!s_active) return;
    NimBLEDevice::deinit(true);
    s_active = false;
    s_on_connected = nullptr;
    Serial.println("[Improv] BLE stopped and memory released");
}

bool improv_ble_is_active() {
    return s_active;
}

bool improv_ble_reboot_pending() {
    return s_reboot_pending && (uint32_t)(millis() - s_reboot_at_ms) < 0x80000000UL;
}

#endif
```

- [ ] **Step 3: Verify it still compiles in the SoftAP build (must be inert)**

Run: `pio run -e esp32-furnace-thermostat`
Expected: SUCCESS. With no `IMPROV_WIFI_BLE_ENABLED` flag and no NimBLE in `lib_deps` yet, both files compile to nothing (header `#ifdef` / source `#if defined`). This confirms the guards keep the module out of the SoftAP build.

- [ ] **Step 4: Commit**

```bash
git add include/improv_ble_provisioning.h src/improv_ble_provisioning.cpp
git commit -m "feat(provision): revive improv_ble_provisioning module (no BT-mem pin)

Restores the BLE/Improv GATT module removed in f44767a, minus the
esp32-hal-bt-mem.h pin (BT memory is now gated per-boot by btInUse()).
Keeps the reboot-trick (persist creds + reboot; never connect with BLE
up) and the ADV/scan-response 31-byte packet fix. Guarded off unless
IMPROV_WIFI_BLE_ENABLED.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: BLE branch in `run_provisioning_boot()`

**Files:**
- Modify: `src/thermostat/esp32s3_thermostat_firmware.cpp` (`run_provisioning_boot()` ~lines 2725-2790; include ~line 17)

- [ ] **Step 1: Add the module include (guarded)**

After the `#include "provisioning_gate.h"` block added in Task 2, add:

```cpp
#ifdef THERMOSTAT_BLE_PROVISIONING
#include "improv_ble_provisioning.h"
#endif
```

- [ ] **Step 2: Branch the provisioning start + on-screen copy**

In `run_provisioning_boot()`, the SoftAP build starts the portal with `g_wifi.start_provisioning()` and shows the AP name. Replace the provisioning-start + LCD-copy region so BLE is used when selected. Find this block (current SoftAP code):

```cpp
  g_wifi.start_provisioning();
  const String ap_ssid = softap_provisioning_ap_ssid();

  if (disp_ok) {
    // Provisioning screen: dark background, AP name to join.
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &thermostat_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *body = lv_label_create(scr);
    const String msg =
        String("Join Wi-Fi network:\n") + ap_ssid + "\nthen open any web page";
    lv_label_set_text(body, msg.c_str());
    lv_obj_set_style_text_color(body, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_set_style_text_font(body, &thermostat_font_montserrat_20, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 20);

    ledcWrite(kBacklightPin, 200);
  } else {
    Serial.printf("[provision] Display unavailable — portal AP: %s\n", ap_ssid.c_str());
  }
```

Replace it entirely with:

```cpp
#ifdef THERMOSTAT_BLE_PROVISIONING
  // BLE/Improv provisioning: persist creds via set_credentials (pure NVS write — does NOT
  // bring up WiFi STA, so BLE keeps the internal RAM it needs), then reboot. On next boot
  // creds are present, btInUse() returns false, BT memory is released, WiFi comes up.
  ImprovBleConfig icfg = {};
  icfg.device_name = "Thermostat";
  icfg.firmware_name = THERMOSTAT_PROJECT_NAME;
  icfg.firmware_version = THERMOSTAT_FIRMWARE_VERSION;
  icfg.hardware_variant = "ESP32-S3";
  icfg.device_url = nullptr;  // we never connect here; no URL to advertise
  icfg.reboot_after_provision = true;
  improv_ble_start(icfg, [](const char *ssid, const char *password) {
    g_wifi.set_credentials(ssid, password);
  });

  if (disp_ok) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &thermostat_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *body = lv_label_create(scr);
    lv_label_set_text(body, "Set up over Bluetooth using\nthe Improv app or improv-wifi.com");
    lv_obj_set_style_text_color(body, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_set_style_text_font(body, &thermostat_font_montserrat_20, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 20);

    ledcWrite(kBacklightPin, 200);
  } else {
    Serial.println("[provision] Display unavailable — provisioning headless via BLE only");
  }
#else
  g_wifi.start_provisioning();
  const String ap_ssid = softap_provisioning_ap_ssid();

  if (disp_ok) {
    // Provisioning screen: dark background, AP name to join.
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &thermostat_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *body = lv_label_create(scr);
    const String msg =
        String("Join Wi-Fi network:\n") + ap_ssid + "\nthen open any web page";
    lv_label_set_text(body, msg.c_str());
    lv_obj_set_style_text_color(body, lv_color_make(0xCC, 0xCC, 0xCC), 0);
    lv_obj_set_style_text_font(body, &thermostat_font_montserrat_20, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 20);

    ledcWrite(kBacklightPin, 200);
  } else {
    Serial.printf("[provision] Display unavailable — portal AP: %s\n", ap_ssid.c_str());
  }
#endif
```

- [ ] **Step 3: Branch the service/reboot loop**

Find the current SoftAP service loop in `run_provisioning_boot()`:

```cpp
  esp_task_wdt_add(NULL);
  uint32_t last_tick = millis();
  for (;;) {
    esp_task_wdt_reset();
    const uint32_t now = millis();
    if (g_wifi.has_credentials()) {
      Serial.println("[provision] Credentials received — rebooting");
      delay(800);  // let the portal's "Saved" response flush to the browser
      ESP.restart();
    }
    g_wifi.ensure_connected(now);  // runs the DNS + portal web server
    if (disp_ok && (now - last_tick) >= kUiTickMs) {
      lv_tick_inc(now - last_tick);
      last_tick = now;
      lv_timer_handler();
    }
    delay(1);
  }
```

Replace it with a branched loop. The BLE loop watches `improv_ble_reboot_pending()` (creds already saved by the callback; NimBLE GATT is serviced by the BLE stack task, so no per-loop service call is needed). The SoftAP loop is unchanged:

```cpp
  esp_task_wdt_add(NULL);
  uint32_t last_tick = millis();
  for (;;) {
    esp_task_wdt_reset();
    const uint32_t now = millis();
#ifdef THERMOSTAT_BLE_PROVISIONING
    if (improv_ble_reboot_pending()) {
      Serial.println("[provision] Credentials received — rebooting");
      ESP.restart();
    }
#else
    if (g_wifi.has_credentials()) {
      Serial.println("[provision] Credentials received — rebooting");
      delay(800);  // let the portal's "Saved" response flush to the browser
      ESP.restart();
    }
    g_wifi.ensure_connected(now);  // runs the DNS + portal web server
#endif
    if (disp_ok && (now - last_tick) >= kUiTickMs) {
      lv_tick_inc(now - last_tick);
      last_tick = now;
      lv_timer_handler();
    }
    delay(1);
  }
```

- [ ] **Step 4: Verify SoftAP build still compiles (BLE branch not yet active)**

Run: `pio run -e esp32-furnace-thermostat`
Expected: SUCCESS — the `#else` (SoftAP) branches compile; `THERMOSTAT_BLE_PROVISIONING` is not defined yet so no BLE code is reached. (Confirms the SoftAP path is byte-for-byte preserved.)

- [ ] **Step 5: Commit**

```bash
git add src/thermostat/esp32s3_thermostat_firmware.cpp
git commit -m "feat(provision): BLE/Improv branch in run_provisioning_boot

Under THERMOSTAT_BLE_PROVISIONING the display provisioning boot starts
Improv over BLE (creds persisted via set_credentials, then reboot) and
shows Bluetooth setup copy. SoftAP path preserved under #else.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Build wiring — flip main env to BLE + add SoftAP env

**Files:**
- Modify: `platformio.ini` (`[env:esp32-furnace-thermostat]`, lines ~157-226; add new env after it)

- [ ] **Step 1: Add NimBLE + Improv to the display env `lib_deps`**

In `[env:esp32-furnace-thermostat]`, find:

```ini
lib_deps =
  lvgl/lvgl@^8.3.11
  adafruit/Adafruit AHTX0@^2.0.5
  adafruit/Adafruit Si7021 Library@^1.5.3
  adafruit/Adafruit Unified Sensor@^1.1.15
  knolleary/PubSubClient@^2.8
```

Replace with:

```ini
lib_deps =
  lvgl/lvgl@^8.3.11
  adafruit/Adafruit AHTX0@^2.0.5
  adafruit/Adafruit Si7021 Library@^1.5.3
  adafruit/Adafruit Unified Sensor@^1.1.15
  knolleary/PubSubClient@^2.8
  jnthas/Improv WiFi Library@^0.0.4
  h2zero/NimBLE-Arduino@^2.3.7
```

- [ ] **Step 2: Add the BLE build flags**

In the same env, find:

```ini
build_flags =
  ${env.build_flags}
  -DBOARD_HAS_PSRAM
  -DTHERMOSTAT_ROLE_THERMOSTAT
  -DLV_CONF_INCLUDE_SIMPLE
  -DLV_CONF_PATH=lv_conf.h
  -Iinclude
  -DCONFIG_ENABLE_ARDUINO_DEPENDS
```

Add the two BLE flags at the end:

```ini
build_flags =
  ${env.build_flags}
  -DBOARD_HAS_PSRAM
  -DTHERMOSTAT_ROLE_THERMOSTAT
  -DLV_CONF_INCLUDE_SIMPLE
  -DLV_CONF_PATH=lv_conf.h
  -Iinclude
  -DCONFIG_ENABLE_ARDUINO_DEPENDS
  -DIMPROV_WIFI_BLE_ENABLED
  -DTHERMOSTAT_BLE_PROVISIONING
```

- [ ] **Step 3: Enable BT + apply the Tier 1 NimBLE config**

In the same env, find the existing NimBLE lines in `custom_sdkconfig`:

```ini
  CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED=y
  CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED=y
  CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
  CONFIG_BT_NIMBLE_MAX_BONDS=3
  CONFIG_BT_NIMBLE_MAX_CCCDS=6
```

Replace with the Tier 1 minimal-Improv set (enables BT, drops security/bond-persist, no BLE-5.0 ext-adv, caps connections):

```ini
  CONFIG_BT_ENABLED=y
  CONFIG_BT_NIMBLE_ENABLED=y
  CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED=y
  CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED=y
  CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
  CONFIG_BT_NIMBLE_MAX_BONDS=1
  CONFIG_BT_NIMBLE_MAX_CCCDS=6
  CONFIG_BT_NIMBLE_SECURITY_ENABLE=n
  CONFIG_BT_NIMBLE_NVS_PERSIST=n
  CONFIG_BT_NIMBLE_EXT_ADV=n
  CONFIG_BT_LE_MAX_CONNECTIONS=1
  CONFIG_BT_LE_50_FEATURE_SUPPORT=n
```

> If the build errors on an unknown `CONFIG_BT_LE_*`/`CONFIG_BT_NIMBLE_*` key (names vary by framework version), confirm the exact key in `~/.platformio/packages/framework-arduinoespressif32` (Kconfig) and adjust. `CONFIG_BT_NIMBLE_MAX_BONDS` must stay ≥ 1 — `0` breaks the IDF NimBLE store build.

- [ ] **Step 4: Add the SoftAP fallback env**

Immediately after the `[env:esp32-furnace-thermostat]` block (before the next `[env:...]`), add a SoftAP variant that inherits everything and removes the BLE pieces:

```ini
; SoftAP-portal display build (rollback / safety path). Identical to
; esp32-furnace-thermostat but without BLE/Improv: no NimBLE, no BLE flags, BT disabled.
[env:esp32-furnace-thermostat-softap]
extends = env:esp32-furnace-thermostat
lib_deps =
  lvgl/lvgl@^8.3.11
  adafruit/Adafruit AHTX0@^2.0.5
  adafruit/Adafruit Si7021 Library@^1.5.3
  adafruit/Adafruit Unified Sensor@^1.1.15
  knolleary/PubSubClient@^2.8
build_flags =
  ${env.build_flags}
  -DBOARD_HAS_PSRAM
  -DTHERMOSTAT_ROLE_THERMOSTAT
  -DLV_CONF_INCLUDE_SIMPLE
  -DLV_CONF_PATH=lv_conf.h
  -Iinclude
  -DCONFIG_ENABLE_ARDUINO_DEPENDS
custom_sdkconfig =
  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
  CONFIG_ESP32S3_DATA_CACHE_64KB=y
  CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
  CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
  CONFIG_SPIRAM_RODATA=y
  CONFIG_LCD_RGB_ISR_IRAM_SAFE=y
  CONFIG_LCD_RGB_RESTART_IN_VSYNC=y
  CONFIG_GDMA_CTRL_FUNC_IN_IRAM=y
  CONFIG_GDMA_ISR_IRAM_SAFE=y
```

> `extends` inherits `platform`, `board`, `board_build.*`, `custom_component_remove`, `lib_ignore`, and `build_src_filter`. We override `lib_deps`, `build_flags`, and `custom_sdkconfig` to drop NimBLE/BT and the BLE flags — yielding today's SoftAP build.

- [ ] **Step 5: Build the BLE env (full link with NimBLE)**

Run: `pio run -e esp32-furnace-thermostat`
Expected: SUCCESS. First build is slow (HybridCompile rebuilds ESP-IDF from source because `custom_sdkconfig` changed). NimBLE + Improv link in. If parallel `ar` fails intermittently, just re-run (known race).

- [ ] **Step 6: Build the SoftAP env (no NimBLE)**

Run: `pio run -e esp32-furnace-thermostat-softap`
Expected: SUCCESS — proves the compile-time switch links the SoftAP path with no NimBLE/BT.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini
git commit -m "build(thermostat): flip display env to BLE/Improv; add -softap env

esp32-furnace-thermostat now builds BLE/Improv provisioning (NimBLE +
Improv lib_deps, BLE flags, Tier 1 minimal NimBLE sdkconfig with BT
enabled, security/bond-persist/ext-adv off). New esp32-furnace-thermostat
-softap retains the SoftAP build for rollback.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Full verification + docs/memory update

**Files:**
- Modify: `tasks/todo.md` (review section)
- Modify: `/home/ryan/.claude/projects/-home-ryan-github-rgregg-esp32-wireless-thermostat/memory/project_ble_provisioning_revival.md` and `MEMORY.md` (status update)

- [ ] **Step 1: Native tests green**

Run: `pio run -e native-tests && .pio/build/native-tests/program`
Expected: all tests pass (incl. `provisioning_gate_*`), exit code 0.

- [ ] **Step 2: All three relevant envs build**

Run: `pio run -e esp32-furnace-thermostat && pio run -e esp32-furnace-thermostat-softap && pio run -e esp32-furnace-thermostat-test`
Expected: all SUCCESS. (`-test` env confirms the BLE-default change didn't break the test display build; if `-test` extends the main env and now pulls BLE, that is expected and fine.)

- [ ] **Step 3: Record the deferred on-bench verification**

This is the Scenario-A measurement the experiment deferred. It **requires hardware** (bench ESP32-S3 via piserial5) and is therefore a manual follow-up, not an automated step. Document it in `tasks/todo.md` as the acceptance gate before flashing production:
- Flash `esp32-furnace-thermostat` to a creds-cleared bench S3; confirm provisioning boot: BLE + LCD + LVGL UI coexist (log largest-contiguous internal-DMA free; expect > 159 KB).
- Provision via web.improv-wifi.com or HA; confirm reboot.
- Normal boot: WiFi connects; log free-heap delta vs. `-softap` build to confirm ~36 KB BT memory reclaimed.
- Serial: confirm whether the app console enumerates on `/dev/ttyUSB0` (CH340) or `/dev/ttyACM1` (USB-JTAG) for this build.

- [ ] **Step 4: Update project memory**

Update `project_ble_provisioning_revival.md` to note implementation landed (branch/PR), Tier 1 trims applied, and that the on-bench Scenario-A measurement is the remaining acceptance gate. Update the `MEMORY.md` Active Work line accordingly.

- [ ] **Step 5: Commit**

```bash
git add tasks/todo.md
git commit -m "docs(provision): record BLE revival verification + on-bench acceptance gate

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Spec §1 shared predicate → Tasks 1 (pure) + 2 (wrapper + setup refactor). ✓
- Spec §2 `btInUse()` override → Task 2. ✓
- Spec §3 revive module sans BT-mem pin, keep reboot-trick + ADV fix → Task 3. ✓
- Spec §4 BLE branch in `run_provisioning_boot()` → Task 4. ✓
- Spec §5 build wiring (flip main env, add `-softap`, controller untouched) → Task 5. ✓
- Spec §6 Tier 1 NimBLE config; Tier 2 deferred → Task 5 (Tier 1 applied; Tier 2 not in plan, by design). ✓
- Spec Testing (native, both envs, Scenario-A) → Task 6. ✓
- Spec error handling (fail-safe retain BT mem on NVS unreadable; headless LCD) → Task 2 wrapper comment + Task 4 headless `#else`/BLE branch. ✓

**Placeholder scan:** No TBD/TODO/"add error handling"/"similar to Task N". All code shown in full. ✓

**Type/name consistency:** `provisioning_gate::needed(bool,const char*,const char*)` defined Task 1, called Task 2. `thermostat_provisioning_needed()` defined Task 2, called by `btInUse()` (Task 2) and `setup()` (Task 2). `improv_ble_start`/`improv_ble_reboot_pending`/`ImprovBleConfig` defined Task 3, used Task 4. `set_credentials(const char*,const char*)` is the existing manager method. Flags `IMPROV_WIFI_BLE_ENABLED` / `THERMOSTAT_BLE_PROVISIONING` used consistently. ✓
