# Controller ESP32-S3 Feather Hardware Port Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an I2C relay backend to the controller firmware supporting the Adafruit ESP32-S3 Feather + XL9535-K4V5 relay module, alongside the existing GPIO-based ESP32 environment.

**Architecture:** Extend `ControllerRelayIoConfig` with a `use_i2c` flag and XL9535 I2C parameters. `ControllerRelayIo::write_outputs()` branches on the flag: GPIO path unchanged, I2C path writes a bitmask to the XL9535 output register. A new `esp32-furnace-controller-s3` PlatformIO environment selects the I2C path via `-DCONTROLLER_HW_XL9535`. No new abstractions — interlock state machine is untouched.

**Tech Stack:** Arduino/ESP-IDF via PlatformIO, Wire (Arduino I2C), XL9535 (PCA9535-compatible I2C I/O expander), C++11.

---

## Chunk 1: Relay IO I2C Backend

### Task 1: Extend `ControllerRelayIoConfig` with I2C fields

**Files:**
- Modify: `include/controller/controller_relay_io.h`

The existing struct is at lines 9–17. Add four new fields after `default_interlock_wait_ms`. The `TwoWire*` field must be inside `#if defined(ARDUINO)` to keep native/simulator builds working — they compile this header without Arduino libraries.

- [ ] **Open `include/controller/controller_relay_io.h`**

- [ ] **Add I2C fields to `ControllerRelayIoConfig` after line 16:**

```cpp
struct ControllerRelayIoConfig {
  int heat_pin = 32;
  int cool_pin = 33;
  int fan_pin = 25;
  int spare_pin = 26;
  bool inverted = false;
  uint32_t heat_interlock_wait_ms = 500;
  uint32_t default_interlock_wait_ms = 1000;
  // I2C relay backend (use_i2c = true → XL9535 via Wire; false → GPIO above)
  bool use_i2c = false;
#if defined(ARDUINO)
  TwoWire *i2c_bus = &Wire;
#endif
  uint8_t i2c_address = 0x20;
  uint8_t i2c_relay_offset = 0;  // bit offset within port 0 (0 = relay1 on bit 0)
};
```

Also add the required include at the top of the header, inside an Arduino guard, so `TwoWire` is declared:

```cpp
#pragma once

#include <stdint.h>

#if defined(ARDUINO)
#include <Wire.h>
#endif

#include "thermostat_types.h"
```

- [ ] **Verify native tests and existing controller both still compile:**

```bash
cd /Users/ryan/github/rgregg/esp32-wireless-thermostat
pio run -e native-tests
pio run -e esp32-furnace-controller
```

Expected: both `SUCCESS` — no new errors. The `TwoWire*` field is hidden from non-Arduino builds; the existing GPIO controller env is unaffected.

- [ ] **Commit:**

```bash
git add include/controller/controller_relay_io.h
git commit -m "feat: add I2C fields to ControllerRelayIoConfig for XL9535 backend"
```

---

### Task 2: Add I2C backend to `controller_relay_io.cpp`

**Files:**
- Modify: `src/controller/controller_relay_io.cpp`

Two changes:
1. `begin()` — skip `pinMode()` calls and run XL9535 init when `use_i2c = true`
2. `write_outputs()` — add I2C bitmask write branch inside the existing `#if defined(ARDUINO)` block

- [ ] **Add `#include <Wire.h>` inside the existing Arduino guard at the top of the file** (after the existing `#include <Arduino.h>` at line 4):

```cpp
#if defined(ARDUINO)
#include <Arduino.h>
#include <Wire.h>
#endif
```

- [ ] **Modify `begin()` at line 16–28** to skip GPIO setup and run XL9535 init when `use_i2c`:

```cpp
void ControllerRelayIo::begin() {
#if defined(ARDUINO)
  if (config_.use_i2c) {
    // Configure XL9535 port 0 as all-outputs (register 0x06 = 0x00)
    config_.i2c_bus->beginTransmission(config_.i2c_address);
    config_.i2c_bus->write(0x06);  // config register, port 0
    config_.i2c_bus->write(0x00);  // all outputs
    uint8_t result = config_.i2c_bus->endTransmission();
    Serial.printf("[RelayIO] XL9535 init at 0x%02X: %s (code %d)\n",
                  config_.i2c_address,
                  result == 0 ? "OK" : "FAILED",
                  result);
  } else {
    pinMode(config_.heat_pin, OUTPUT);
    pinMode(config_.cool_pin, OUTPUT);
    pinMode(config_.fan_pin, OUTPUT);
    pinMode(config_.spare_pin, OUTPUT);
  }
#endif
  write_outputs(RelayDemand{});
  initialized_ = true;
  active_ = RelaySelect::None;
  pending_ = RelaySelect::None;
  pending_since_ms_ = 0;
}
```

- [ ] **Modify `write_outputs()` at line 70–80** to add I2C branch inside the existing `#if defined(ARDUINO)` block:

```cpp
void ControllerRelayIo::write_outputs(const RelayDemand &out) {
  output_ = out;
#if defined(ARDUINO)
  if (config_.use_i2c) {
    const uint8_t relay_bits = (out.heat ? 1 : 0) |
                               (out.cool ? 2 : 0) |
                               (out.fan  ? 4 : 0) |
                               (out.spare? 8 : 0);
    const uint8_t relay_mask = static_cast<uint8_t>(0x0F << config_.i2c_relay_offset);
    const uint8_t shifted    = static_cast<uint8_t>(relay_bits << config_.i2c_relay_offset);
    const uint8_t bitmask    = config_.inverted
                               ? static_cast<uint8_t>(relay_mask & ~shifted)
                               : shifted;
    config_.i2c_bus->beginTransmission(config_.i2c_address);
    config_.i2c_bus->write(0x02);   // output port 0 register
    config_.i2c_bus->write(bitmask);
    uint8_t result = config_.i2c_bus->endTransmission();
    if (result != 0) {
      Serial.printf("[RelayIO] I2C write failed (code %d)\n", result);
    }
  } else {
    const bool on = relay_on_level(config_.inverted);
    const bool off = relay_off_level(config_.inverted);
    digitalWrite(config_.heat_pin, out.heat ? on : off);
    digitalWrite(config_.cool_pin, out.cool ? on : off);
    digitalWrite(config_.fan_pin,  out.fan  ? on : off);
    digitalWrite(config_.spare_pin,out.spare? on : off);
  }
#endif
}
```

- [ ] **Verify native tests still compile and pass:**

```bash
pio run -e native-tests && ./.pio/build/native-tests/program
```

Expected: all tests pass. The `use_i2c` branch is never executed in native builds (it's inside `#if defined(ARDUINO)`), but the struct compiles correctly with `use_i2c = false` default.

- [ ] **Commit:**

```bash
git add src/controller/controller_relay_io.cpp
git commit -m "feat: add XL9535 I2C relay backend to ControllerRelayIo"
```

---

### Task 3: Wire up `esp32_controller_main.cpp`

**Files:**
- Modify: `src/esp32_controller_main.cpp`

Two changes:
1. Replace the bare `g_relay_io` global declaration (line 43) with a file-scope config + constructed relay IO
2. Add `Wire.begin()` call in `setup()` before `g_relay_io.begin()` (line 2048)

- [ ] **Replace line 43** (`thermostat::ControllerRelayIo g_relay_io;`) with:

```cpp
#ifdef CONTROLLER_HW_XL9535
static thermostat::ControllerRelayIoConfig s_relay_cfg = []() {
  thermostat::ControllerRelayIoConfig c;
  c.use_i2c     = true;
  c.i2c_address = 0x20;  // XL9535 default; adjust if A0/A1/A2 jumpers differ
  return c;
}();
#else
static thermostat::ControllerRelayIoConfig s_relay_cfg;  // GPIO defaults: pins 32/33/25/26
#endif
thermostat::ControllerRelayIo g_relay_io(s_relay_cfg);
```

- [ ] **In `setup()`, before `g_relay_io.begin()`** (search by content — line number shifts after the previous edit in this task), add:

```cpp
#ifdef CONTROLLER_HW_XL9535
  Wire.begin(3, 4);  // Feather S3: SDA=GPIO3, SCL=GPIO4
#endif
  g_relay_io.begin();
```

- [ ] **Verify native-tests still compile and pass** (regression — main.cpp is excluded from native builds but its headers must be consistent):

```bash
pio run -e native-tests && ./.pio/build/native-tests/program
```

Expected: all tests pass.

- [ ] **Commit:**

```bash
git add src/esp32_controller_main.cpp
git commit -m "feat: configure XL9535 relay backend in controller main via CONTROLLER_HW_XL9535"
```

---

## Chunk 2: Build System and CI

### Task 4: Add `esp32-furnace-controller-s3` PlatformIO environment

**Files:**
- Modify: `platformio.ini`

Add the new env after the existing `[env:esp32-furnace-controller]` block (after line 125). It duplicates all keys from the existing controller env (matching project convention — no `extends`), changes `board` and `board_build.partitions`, and adds `-DCONTROLLER_HW_XL9535` to `build_flags`.

> **Note on partition file:** `default.csv` is the standard PlatformIO 4MB partition scheme. If `esptool.py flash_id` shows 8MB flash, use `default_8MB.csv` instead. Both ship with the arduino-espressif32 framework at `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`.

- [ ] **Add after the `[env:esp32-furnace-controller]` block (after line 125):**

```ini
[env:esp32-furnace-controller-s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.37/platform-espressif32.zip
board = adafruit_feather_esp32s3_nopsram
framework = arduino
board_build.partitions = default.csv
; Reduce TLS heap footprint for HTTPS weather fetches (same as controller)
custom_sdkconfig =
  CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH=y
  CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
  CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
  CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
; Strip managed components we don't need
custom_component_remove =
  espressif/esp_hosted
  espressif/esp_wifi_remote
  espressif/esp-dsp
  espressif/esp_insights
  espressif/esp_rainmaker
  espressif/rmaker_common
  espressif/esp_schedule
  espressif/json_generator
  espressif/json_parser
  espressif/esp_diagnostics
  espressif/cbor
  espressif/esp_diag_data_store
  espressif/esp-zboss-lib
  espressif/esp-zigbee-lib
  espressif/esp-sr
  espressif/esp-modbus
  espressif/esp_modem
  espressif/qrcode
  espressif/network_provisioning
  chmorgan/esp-libhelix-mp3
  espressif/fb_gfx
  espressif/dl_fft
  espressif/esp-serial-flasher
lib_deps =
  knolleary/PubSubClient@^2.8
  jnthas/Improv WiFi Library@^0.0.4
  h2zero/NimBLE-Arduino@^2.3.7
build_src_filter =
  +<*>
  -<main.cpp>
  -<esp32_thermostat_main.cpp>
  -<thermostat/esp32s3_thermostat_firmware.cpp>
  -<thermostat/ui/*>
  -<thermostat/ui/fonts/*>
  -<sim/*>
  -<bootstrap/*>
build_flags =
  ${env.build_flags}
  -DTHERMOSTAT_ROLE_CONTROLLER
  -DIMPROV_WIFI_BLE_ENABLED
  -DCONTROLLER_HW_XL9535
```

- [ ] **Attempt a local build to catch any obvious issues:**

```bash
pio run -e esp32-furnace-controller-s3
```

Expected: `SUCCESS`. If the board ID is wrong, PlatformIO will error with "Unknown board". Correct IDs to try: `adafruit_feather_esp32s3_nopsram`, `adafruit_feather_esp32s3`. If partition file is wrong, error will say "Partition table file not found" — check `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/` for the correct filename.

- [ ] **Verify the original controller env still builds** (regression):

```bash
pio run -e esp32-furnace-controller
```

Expected: `SUCCESS`.

- [ ] **Commit:**

```bash
git add platformio.ini
git commit -m "feat: add esp32-furnace-controller-s3 PlatformIO environment for Feather S3 + XL9535"
```

---

### Task 5: Add CI build step for the S3 environment

**Files:**
- Modify: `.github/workflows/ci.yml`

Add a new step after the existing "Build controller firmware" step (after line 44).

- [ ] **Add after the "Build controller firmware" step:**

```yaml
      - name: Build controller S3 firmware
        uses: nick-fields/retry@v3
        with:
          timeout_minutes: 15
          max_attempts: 2
          command: pio run -e esp32-furnace-controller-s3
```

- [ ] **Commit:**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: add esp32-furnace-controller-s3 build step"
```

- [ ] **Push branch and verify CI goes green:**

```bash
git push -u origin controller-esp32s3-feather
```

Then watch `gh run list --branch controller-esp32s3-feather` until all jobs pass.

---

## Hardware Bring-up (post-implementation, when hardware arrives)

These steps are not code changes — they're the physical bring-up checklist once the Feather S3 and XL9535-K4V5 module arrive.

1. **Confirm flash size:** `esptool.py --port /dev/tty.xxx flash_id` — update `board_build.partitions` in platformio.ini if not 4MB
2. **Serial flash:** `pio run -e esp32-furnace-controller-s3 --target upload --upload-port /dev/tty.xxx`
3. **Open serial monitor** — look for `[RelayIO] XL9535 init at 0x20: OK (code 0)`. If FAILED, check SDA/SCL wiring and I2C address jumpers on the K4V5 board
4. **Test relays via MQTT:** publish to `<device>/cmd/hvac` and verify correct relay energizes with a multimeter or LED
5. **Adjust config if needed:** if relay mapping is wrong, update `i2c_relay_offset` in `s_relay_cfg`; if polarity is wrong, set `inverted = true`. Reflash.
6. **Run OTA soak test:** `CONTROLLER_IP=<feather-ip> ./scripts/ota_soak_test.sh controller`
