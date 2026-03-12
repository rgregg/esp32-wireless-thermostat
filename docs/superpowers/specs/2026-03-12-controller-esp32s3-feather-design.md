# Controller ESP32-S3 Feather Hardware Port

**Date:** 2026-03-12
**Status:** Draft

## Overview

Port the furnace controller firmware to the Adafruit ESP32-S3 Feather paired with an XL9535-K4V5 4-channel I2C relay module. The primary motivation is moving to a better-supported, easier-to-interface dev board. ESP-NOW, MQTT, OTA, and all existing controller logic are preserved unchanged.

The existing `esp32-furnace-controller` environment (ESP32 classic, direct GPIO relays) is kept alongside the new one as a fallback.

## Hardware

| Component | Details |
|-----------|---------|
| MCU board | Adafruit ESP32-S3 Feather (verify flash size and PSRAM before setting board ID) |
| Relay module | XL9535-K4V5 4-channel I2C relay |
| I2C expander | XL9535 (PCA9535-compatible) |
| Default I2C address | 0x20 (A0/A1/A2 = GND — confirm on board) |
| Feather S3 I2C pins | SDA = GPIO3, SCL = GPIO4 |

### Hardware Verification Checklist

Confirm the following when the hardware arrives before finalizing relay bitmask mappings:

- [ ] Flash size — run `esptool.py flash_id` to confirm 4MB vs 8MB; pick correct PlatformIO board ID
- [ ] PSRAM presence — use `adafruit_feather_esp32s3_nopsram` if no PSRAM, `adafruit_feather_esp32s3` otherwise
- [ ] I2C address — configure relay config with address found by scanning 0x20–0x27 at startup
- [ ] Which XL9535 port pins (P0.x vs P1.x) connect to which relays — set `i2c_relay_offset` accordingly
- [ ] Relay polarity — active-high or active-low (check K4V5 schematic or measure); set `inverted` flag
- [ ] Partition scheme filename — check `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/` for the correct 4MB scheme name (likely `default.csv`)

## Architecture

No new abstractions. The existing `ControllerRelayIo` class keeps its interface and interlock state machine unchanged. The I2C path is a parallel write backend selected by a flag in the config struct, which is constructed at file scope in `esp32_controller_main.cpp`.

```
ControllerRelayIoConfig { use_i2c = true }   ← file-scope, #ifdef-selected
         ↓
ControllerRelayIo g_relay_io(s_relay_cfg)    ← constructed at file scope
         ↓
setup(): Wire.begin(SDA=3, SCL=4)
         g_relay_io.begin()
           → XL9535: write 0x00 to config reg 0x06 (port 0 all-outputs)
           → logs init result
         ↓
write_outputs()    [called from interlock state machine]
  → if use_i2c: Wire write bitmask to XL9535 output reg 0x02; check return
  → else:       digitalWrite() on GPIO pins (existing path)
```

## Changes

### 1. `include/controller/controller_relay_io.h`

Add four fields to `ControllerRelayIoConfig`, guarded by `#if defined(ARDUINO)` so
the non-Arduino (native test / simulator) builds are unaffected:

```cpp
bool    use_i2c           = false;   // false = GPIO mode (existing behavior)
#if defined(ARDUINO)
TwoWire *i2c_bus          = &Wire;   // I2C bus instance
#endif
uint8_t i2c_address       = 0x20;    // XL9535 I2C address
uint8_t i2c_relay_offset  = 0;       // left-shift applied to relay bits within port 0
```

When `use_i2c = false`, all new fields are ignored. Existing behavior is identical.

### 2. `src/controller/controller_relay_io.cpp`

**`begin()`**: if `use_i2c`, skip the existing `pinMode()` calls (they target ESP32 GPIO 32/33/25/26 which may conflict on the Feather S3), then write `0x00` to XL9535 register `0x06` to configure port 0 as all-outputs. Log the configured I2C address and the `Wire.endTransmission()` result (0 = success) so bring-up failures are visible in the serial log. Do NOT call `Wire.begin()` here — the caller is responsible for initializing the bus before calling `begin()`.

**`write_outputs()`**: add branch, wrapped in `#if defined(ARDUINO)` to match the existing pattern and prevent native/simulator build failures:

```
#if defined(ARDUINO)
if use_i2c:
    relay_bits = heat<<0 | cool<<1 | fan<<2 | spare<<3
    relay_mask = 0x0F << i2c_relay_offset
    shifted    = relay_bits << i2c_relay_offset
    bitmask    = inverted ? (relay_mask & ~shifted) : shifted
    Wire.beginTransmission(i2c_address)
    Wire.write(0x02)          // output port 0 register
    Wire.write((uint8_t)bitmask)
    result = Wire.endTransmission()
    if result != 0: log warning with result code
else:
    // existing digitalWrite() path (unchanged)
#endif
```

XL9535 register map (PCA9535-compatible):

| Register | Address | Purpose |
|----------|---------|---------|
| Input port 0 | 0x00 | Read current pin state |
| Output port 0 | 0x02 | Write relay bitmask |
| Config port 0 | 0x06 | 0x00 = all outputs |

### 3. `src/esp32_controller_main.cpp`

Define the relay config at file scope using `#ifdef`, then construct `g_relay_io` from it:

```cpp
#ifdef CONTROLLER_HW_XL9535
static ControllerRelayIoConfig s_relay_cfg = []() {
    ControllerRelayIoConfig c;
    c.use_i2c     = true;
    c.i2c_address = 0x20;
    return c;
}();
#else
static ControllerRelayIoConfig s_relay_cfg;  // defaults: GPIO 32/33/25/26
#endif

thermostat::ControllerRelayIo g_relay_io(s_relay_cfg);
```

In `setup()`, before `g_relay_io.begin()`:

```cpp
#ifdef CONTROLLER_HW_XL9535
    Wire.begin(3, 4);  // Feather S3: SDA=GPIO3, SCL=GPIO4
#endif
    g_relay_io.begin();
```

GPIO pin fields are ignored when `use_i2c = true`. No other changes to `setup()` or `loop()`.

### 4. `platformio.ini`

Add new environment using explicit key duplication (matching the existing project pattern, not `extends`):

```ini
[env:esp32-furnace-controller-s3]
platform = https://github.com/.../platform-espressif32.zip   ; same as controller
board = adafruit_feather_esp32s3_nopsram                      ; verify before use
framework = arduino
board_build.partitions = default.csv                          ; verify filename for actual flash size
custom_sdkconfig =
  ; same TLS optimizations as esp32-furnace-controller
  CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH=y
  CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
  CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
  CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
custom_component_remove =
  ; same component removals as esp32-furnace-controller
  ...
lib_deps =
  knolleary/PubSubClient@^2.8
  jnthas/Improv WiFi Library@^0.0.4
  h2zero/NimBLE-Arduino@^2.3.7
build_src_filter =
  ; same as esp32-furnace-controller
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

### 5. `.github/workflows/ci.yml`

Add build step for the new environment alongside the existing controller step:

```yaml
- name: Build controller S3 firmware
  uses: nick-fields/retry@v3
  with:
    timeout_minutes: 15
    max_attempts: 2
    command: pio run -e esp32-furnace-controller-s3
```

## Bring-up Procedure

1. Serial flash the Feather S3 with `esp32-furnace-controller-s3` firmware
2. Connect relay module: SDA→GPIO3, SCL→GPIO4, VCC, GND
3. Open serial monitor — look for XL9535 init log line with I2C result code (0 = success)
4. If init fails (non-zero result), check wiring and I2C address solder jumpers on the K4V5 board
5. Use MQTT to send heat/cool/fan commands and verify correct relay energizes
6. Adjust `i2c_relay_offset` and `inverted` in config if relay mapping or polarity is wrong, reflash
7. Run OTA soak test once manual bring-up passes

## What Does Not Change

- ESP-NOW transport and thermostat pairing
- MQTT topic structure and HA discovery
- Interlock timing (heat: 500 ms, others: 1000 ms)
- Web server, OTA, WiFi provisioning via BLE Improv
- Native tests (no hardware dependency — `use_i2c = false` default, `TwoWire*` guarded)
