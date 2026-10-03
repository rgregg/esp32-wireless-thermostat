# Display BLE/Improv Provisioning Revival — Design

**Date:** 2026-06-23
**Status:** Approved (design); pending spec review → implementation plan
**Scope:** Display (thermostat) firmware only. Controller firmware unchanged (stays SoftAP).

## Background

BLE/Improv provisioning was removed in `f44767a` and replaced with a SoftAP captive
portal, because NimBLE's internal-DMA-RAM footprint was implicated in the display's
RGB-LCD bounce-buffer crashes. A feasibility experiment
(`experiment/ble-provisioning-mem-probe`) then established:

- **Phase 1 (BLE + LCD, WiFi down):** FIT — 159 KB largest-contiguous internal-DMA RAM
  free with NimBLE up and the RGB panel allocated.
- **Phase 2 (BLE on the full running firmware, WiFi up):** NO-FIT — `esp_wifi_init`
  failed with `ESP_ERR_NO_MEM`; WiFi never came up.

**Conclusion the experiment reached:** BLE/Improv provisioning is viable *only* as a
dedicated provisioning boot with WiFi DOWN, never concurrent with the running firmware.
This matches how provisioning is already entered (no creds → dedicated boot).

This design implements that conclusion: revive BLE/Improv provisioning on the display as
a compile-time-selectable alternative to SoftAP, gated to a WiFi-down provisioning boot,
with BT controller memory **released on every normal (provisioned) boot** so it costs
zero runtime RAM in normal operation.

## Goals

1. Restore the BLE/Improv provisioning UX on the display (provision from a phone/browser
   over Web Bluetooth or Home Assistant; no separate WiFi network to join).
2. Zero BLE runtime cost in normal operation (BT memory released when creds exist).
3. Keep SoftAP fully buildable for the display as a safety/rollback path.
4. Trim NimBLE to the minimum a one-shot Improv peripheral actually needs.

## Non-Goals

- BLE provisioning for the controller firmware (stays SoftAP).
- Concurrent WiFi + BLE operation (proven NO-FIT; explicitly avoided).
- Changing the credential storage format or the SoftAP flow itself.

## Decisions (locked)

| Decision | Choice |
|---|---|
| SoftAP vs BLE on display | **Compile-time switch** — only one path linked per build |
| Shipping env `esp32-furnace-thermostat` | **Flips to BLE** by default |
| SoftAP retained | Yes — as a separate display env (`esp32-furnace-thermostat-softap`) |
| Controller | Unchanged (SoftAP) |
| NimBLE trimming | **Tier 1 baseline now**; Tier 2 aggressive buffer-floor as a bench-validated follow-up |

## Architecture

Five pieces. Each is independently understandable and testable.

### 1. Shared provisioning-entry predicate (correctness keystone)

The decision "should this boot enter provisioning?" is currently inline in
`thermostat_firmware_setup()`:

```
if (!g_cfg_wifi_disabled && g_cfg_wifi_ssid.isEmpty()) { run_provisioning_boot(); ... }
```

`btInUse()` (see §2) must make the **same** decision, but it runs inside `initArduino()`
*before* `setup()` and before the Arduino `Preferences` object is opened. To prevent the
two predicates from drifting (a drift would either waste BT RAM on normal boots or starve
the provisioning boot of it), extract a single free function:

```c
// reads NVS directly via the C API (nvs_open/nvs_get_str on namespace "cfg_disp");
// safe to call from initArduino() (after nvs_flash_init) and from setup().
bool thermostat_provisioning_needed();
```

Predicate (must mirror the existing entry condition exactly):
- WiFi NOT disabled (ESP-NOW-only mode never provisions), AND
- NVS `wifi_ssid` is empty/absent, AND
- no compile-time baked default SSID (`THERMOSTAT_WIFI_SSID` empty).

`setup()` is refactored to call this helper instead of the inline check, so there is one
source of truth. NVS namespace `cfg_disp`, keys `wifi_ssid` and the wifi-disabled flag.

### 2. `btInUse()` override — per-boot BT memory gating

Arduino core `esp32-hal-misc.c:initArduino()` runs `nvs_flash_init()` and *then*
`if (!btInUse()) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM)`. The weak default
`btInUse()` returns `_btLibraryInUse`. We provide a **strong override** (display BLE build
only):

```c
bool btInUse() { return thermostat_provisioning_needed(); }
```

- **No creds (provisioning boot):** `true` → BT controller memory **retained** → BLE can start.
- **Creds present (normal boot):** `false` → core releases BT memory (~36 KB) → **zero BLE
  runtime cost** in normal operation.

This replaces the old `esp32-hal-bt-mem.h` constructor, which pinned BT memory on *every*
boot (the wasteful behavior we are explicitly inverting). The old header is **not** restored.

### 3. `improv_ble_provisioning.{h,cpp}` — provisioning module (revived)

Restore from history (`f44767a^:src/improv_ble_provisioning.cpp` / `.h`) with edits:

- **Remove** `#include "esp32-hal-bt-mem.h"` (replaced by §2).
- **Keep the reboot-trick** (mandated by Phase 2 NO-FIT): `setCustomConnectWiFi` returns
  `true` without connecting → `onImprovConnected` persists creds to NVS + schedules a
  reboot (~2 s, after the Improv response flushes) → the next normal boot connects with
  BLE down.
- **Keep the ADV/scan-response packet fix** (primary ADV = flags + service data ≤ 31 B;
  UUID + name in scan response) — without it NimBLE silently drops the Improv service data.
- Compiles under `#if defined(ARDUINO) && defined(IMPROV_WIFI_BLE_ENABLED)` (native build
  unaffected).

Public surface (unchanged from the old header): `improv_ble_start(cfg, on_connected)`,
`improv_ble_stop()`, `improv_ble_is_active()`, `improv_ble_reboot_pending()`.

### 4. Display provisioning boot — branch in `run_provisioning_boot()`

`run_provisioning_boot()` already is the dedicated WiFi-down boot (init backlight + LCD,
loop forever, reboot on creds). Add a compile-time branch:

```
#ifdef THERMOSTAT_BLE_PROVISIONING
    improv_ble_start(...);                 // instead of g_wifi.start_provisioning()
    // LCD: "WiFi Setup — set up over Bluetooth from your phone"
    // loop: service Improv + LVGL; if improv_ble_reboot_pending() → reboot
#else
    // existing SoftAP path, unchanged
#endif
```

- The Improv `device_url` is unavailable at provisioning time (we never connect, we
  reboot), so it is omitted (`nullptr`), exactly as the old code did.
- The on-LCD copy changes from "Join Wi-Fi network <AP> then open any web page" to a
  Bluetooth/Improv instruction. Headless fallback (LCD init failed) still works: log the
  state to serial.
- WDT handling, tick cadence, and the reboot-on-creds structure are reused as-is.

### 5. Build wiring (`platformio.ini`)

- New build flags: `THERMOSTAT_BLE_PROVISIONING` (selects the BLE boot branch) and
  `IMPROV_WIFI_BLE_ENABLED` (compiles the module). Both set together for the BLE build.
- `esp32-furnace-thermostat` (shipping display env): **add** both flags; **restore**
  `lib_deps` for `NimBLE-Arduino` + the Improv WiFi BLE library; add the Tier 1
  `custom_sdkconfig` block (see §6).
- New env `esp32-furnace-thermostat-softap`: the display build **without** the flags /
  NimBLE — i.e. today's SoftAP build, retained for safety/rollback.
- Controller envs: untouched.

### 6. NimBLE minimal config (Tier 1 baseline)

Applied via `custom_sdkconfig` on the BLE display env. These are the correct settings for
a one-shot, unpaired Improv peripheral — not speculative tuning. Starting point is the
probe's known-good roles-disabled config:

```
# Host roles (already proven in the probe)
CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER_DISABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=1          # 0 breaks the IDF framework build — keep >=1
CONFIG_BT_NIMBLE_MAX_CCCDS=6

# Tier 1 additions — Improv needs no pairing/encryption and no BLE 5.0 ext adv
# CONFIG_BT_NIMBLE_SECURITY_ENABLE is not set
# CONFIG_BT_NIMBLE_NVS_PERSIST is not set
# CONFIG_BT_NIMBLE_EXT_ADV is not set            (already off in probe)
CONFIG_BT_LE_MAX_CONNECTIONS=1                    # controller-side cap
# CONFIG_BT_LE_50_FEATURE_SUPPORT is not set      (no BLE 5.0 extended advertising)
# raise BT log level to reduce code/RAM
```

Exact key names verified against the installed framework during implementation (the
ESP32-S3 uses the `CONFIG_BT_LE_*` controller keys).

**Tier 2 (deferred, bench-validated follow-up):** shrink `CONFIG_BT_LE_ACL_BUF_COUNT`,
HCI event buffer counts, and `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` toward the floor. Only
pursue if the Scenario-A measurement (§Testing) shows we want more margin; these can
affect GATT-transfer reliability, so each step must be measured on-bench.

## Data Flow

```
Fresh device boot
  initArduino(): nvs_flash_init() → btInUse()==true (no creds) → BT mem RETAINED
  setup(): thermostat_provisioning_needed()==true → run_provisioning_boot()
    improv_ble_start() → advertise → phone/HA connects over BLE
    user submits SSID/pwd → setCustomConnectWiFi returns true (no connect)
    onImprovConnected: persist creds to NVS → schedule reboot
    loop sees improv_ble_reboot_pending() → ESP.restart()

Provisioned device boot
  initArduino(): nvs_flash_init() → btInUse()==false (creds present) → BT mem RELEASED (~36 KB)
  setup(): thermostat_provisioning_needed()==false → normal firmware path (WiFi up, BLE absent)
```

## Error Handling

- `improv_ble_start()` returns false (BLE init failed): log to serial; the LCD shows a
  setup-failed message; device stays in the provisioning loop (WDT-fed) so a power-cycle
  retries. (Matches SoftAP's "AP could not start" posture.)
- LCD init failed in provisioning boot: run headless (serial logging), same as today.
- NVS unreadable in `btInUse()`: fail safe by **retaining** BT memory (treat as
  "provisioning needed") — a wasted ~36 KB on a normal boot is recoverable; starving the
  provisioning boot of BT memory is not.

## Testing / Verification

- **Native build** (`native-tests`) stays green — BLE code is `#ifdef ARDUINO`.
- **Both display envs build cleanly:** `esp32-furnace-thermostat` (BLE) and
  `esp32-furnace-thermostat-softap` (SoftAP) — proves the compile-time switch links each
  way and the shared predicate compiles in both.
- **On-bench (piserial5, ESP32-S3) — the deferred Scenario-A measurement:**
  - Provisioning boot: confirm BLE + LCD + LVGL provisioning UI coexist with WiFi down;
    log largest-contiguous internal-DMA free (expect comfortably > Phase 1's 159 KB given
    Tier 1 trims).
  - Provision end-to-end via web.improv-wifi.com (or HA) → verify creds persist → reboot.
  - Normal boot: verify WiFi connects, firmware runs, and **BT memory is released** —
    log free-heap delta vs. a build without BLE to confirm ~36 KB is reclaimed.
  - Bench serial note: depending on env, the app console may be UART0 → `/dev/ttyUSB0`
    (CH340) rather than USB-JTAG `/dev/ttyACM1`. Confirm which enumerates for this build.

## Risks / Open Items

- **Predicate drift** between `btInUse()` and `setup()` — mitigated by the single shared
  helper (§1). This is the one correctness-critical coupling.
- **Improv library availability** — reuse the same `NimBLE-Arduino` + Improv WiFi BLE
  libraries the old build used; pin versions in `lib_deps`.
- **`CONFIG_BT_LE_*` key names** can vary by framework version — verify against the
  installed `framework-arduinoespressif32` during implementation, not from memory.
- Flash budget is not a concern (post-removal the display was at 28.2%; re-adding NimBLE
  ~187 KB → ~31%).
