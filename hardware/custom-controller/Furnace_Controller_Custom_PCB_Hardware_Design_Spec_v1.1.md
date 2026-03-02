# Furnace Controller Custom PCB

## Hardware Design Specification

**Version:** 1.1\
**Date:** 2026-03-02

------------------------------------------------------------------------

# 1. Overview

This document defines the hardware requirements for a custom furnace
controller PCB intended to replace a thermostat control module. The
board integrates:

-   ESP32-S3 MCU (Wi-Fi enabled)
-   4x SPST-NO relay outputs (Heat, Cool, Fan, Spare)
-   24VAC furnace power input
-   12V DC regulated output (remote display power)
-   3.3V system rail
-   USB-C debug/programming interface with power input
-   RS-485 interface (future use)
-   Call-line sense inputs for debugging and verification

The system must operate safely in an HVAC environment and tolerate
simultaneous connection of 24VAC and USB power.

------------------------------------------------------------------------

# 2. Functional Requirements

## 2.1 MCU

-   ESP32-S3 module (WROOM variant recommended)
-   WiFi-enabled operation
-   Auto-programming support via USB-UART bridge
-   EN (reset) and IO0 (boot) access
-   Brownout detection enabled in firmware

------------------------------------------------------------------------

## 2.2 Relay Outputs

Four SPST-NO relays:

  Channel   Function   Action
  --------- ---------- ---------
  Relay 1   Heat       R → W
  Relay 2   Cool       R → Y
  Relay 3   Fan        R → G
  Relay 4   Spare      R → AUX

### Suggested Default GPIO Assignments

GPIO assignments are runtime-configurable via the `ControllerRelayIoConfig` structure
stored in NVS. The pins below are suggested defaults — finalize during PCB routing,
avoiding S3 bootstrap-sensitive pins (GPIO 0, 3, 45, 46).

| Channel   | Function | GPIO (suggested) |
|-----------|----------|:----------------:|
| Relay 1   | Heat     | GPIO 4           |
| Relay 2   | Cool     | GPIO 5           |
| Relay 3   | Fan      | GPIO 6           |
| Relay 4   | Spare    | GPIO 7           |

> **Note:** The current development firmware uses GPIO 32/33/25/26 (original ESP32
> devkit). These pins do not exist on ESP32-S3 — the custom PCB must assign S3-valid
> GPIOs and update the firmware defaults to match.

Relay logic: active HIGH (non-inverted). The firmware also supports an `inverted` flag
for active-LOW drivers if needed.

### Requirements

-   12V coil relays
-   Contacts rated ≥1A @ 30VAC minimum
-   Default state: OPEN (fail-safe)
-   Low-side MOSFET driver per relay
-   Flyback diode per coil
-   Hardware gate pulldown resistors
-   Per-channel status LED

------------------------------------------------------------------------

# 3. Power Architecture

## 3.1 24VAC Input

-   Input terminals: R and C
-   Full bridge rectifier
-   Bulk capacitance on rectified bus
-   Input fuse or resettable PTC
-   Surge suppression (TVS or MOV)
-   Designed for 18--32 VAC operating range

------------------------------------------------------------------------

## 3.2 System Rail (12V_SYS)

Primary internal system rail: **12V_SYS**

### Sources:

1.  24VAC → Rectified DC → Wide-input buck → 12V_SYS\
2.  USB 5V → Boost/Buck-Boost → 12V_SYS

### Requirements:

-   Ideal diode OR-ing between sources
-   Reverse current blocking on both inputs
-   No backfeed into USB port
-   Minimum 1.5A peak capability
-   1.0A continuous recommended

------------------------------------------------------------------------

## 3.3 3.3V Rail

-   12V_SYS → Buck to 3.3V
-   ≥700mA peak support
-   Local bulk capacitance near ESP32
-   Reset supervisor recommended

------------------------------------------------------------------------

## 3.4 12V Output (Display Power)

-   Output connector: +12V, GND
-   Reverse current blocking from output to system
-   Resettable fuse or eFuse
-   TVS protection
-   Designed for up to 1A load

------------------------------------------------------------------------

# 4. USB-C Debug & Power

## 4.1 USB Interface

-   USB-C UFP (device mode)
-   5.1k pulldown resistors on CC1/CC2
-   USB-UART bridge (CP2102N or similar)
-   Auto-program circuitry using DTR/RTS
-   ESD protection on D+/D-

## 4.2 USB Power

-   VBUS through eFuse or power switch
-   Reverse current blocking
-   Supports simultaneous AC + USB powering

------------------------------------------------------------------------

# 5. RS-485 Interface (Future Use)

> **Status:** No firmware support today. Hardware is included for future expansion.

-   RS-485 transceiver
-   A/B/GND connector (3-pin minimum)
-   Optional 120Ω termination resistor (jumper controlled)
-   Optional bias resistors footprint
-   Controlled via ESP32 UART + DE/RE GPIO

------------------------------------------------------------------------

# 6. Call-Line Sense Inputs (Future Use)

> **Status:** No firmware support today. Hardware is included for diagnostics and
> future expansion. These inputs allow verifying that relay closures actually
> energize the corresponding call lines.

Sense inputs for:

-   W
-   Y
-   G
-   AUX
-   Optional: R presence

### Requirements

-   High impedance input (≥100kΩ equivalent)
-   AC tolerant
-   Rectified and filtered
-   Clamped to 3.3V
-   RC filter (10--100ms time constant)
-   ADC-readable preferred

------------------------------------------------------------------------

# 7. WiFi & Wireless Communication

The controller uses the ESP32-S3's built-in radio for all wireless communication.
No additional RF hardware is required beyond the module's integrated antenna (or
an external antenna if using a U.FL variant).

## 7.1 WiFi

-   Station (STA) mode — connects to an existing access point
-   Used for MQTT broker connection, OTA updates, web UI, and mDNS

## 7.2 ESP-NOW

-   Point-to-point low-latency communication with the remote display
-   Default channel: 6 (configurable at runtime via NVS key `esp_ch`)
-   Supports broadcast (FF:FF:FF:FF:FF:FF) and unicast peer addressing
-   Used to receive temperature/humidity sensor data and send HVAC state updates
-   Encrypted with a configurable link master key (NVS key `esp_lmk`)

## 7.3 BLE Provisioning

-   Improv-WiFi via NimBLE for initial WiFi credential setup
-   BLE and WiFi are mutually exclusive — BLE is active only during provisioning
    before WiFi is connected

------------------------------------------------------------------------

# 8. MQTT

-   Connects to a configurable MQTT broker (default: `mqtt.lan:1883`)
-   Publishes Home Assistant MQTT discovery payloads for auto-configuration
-   Publishes telemetry: relay states, HVAC mode, fan mode, connectivity status
-   Subscribes to command topics for remote control (mode, setpoint, fan, etc.)
-   Configurable base topic (default: `thermostat/furnace-controller`)

------------------------------------------------------------------------

# 9. OTA & Web Server

## 9.1 ArduinoOTA

-   UDP-based OTA on port 3232
-   Hostname: MAC-suffixed (e.g., `esp32-furnace-controller-AABBCC`)
-   Optional password protection (NVS key `ota_pwd`)

## 9.2 Web OTA

-   HTTP POST to `/update` accepts a firmware binary (multipart form upload)
-   Served on port 80 alongside the configuration UI

## 9.3 Web Configuration UI

-   HTTP server on port 80
-   `/config` endpoint for GET/POST of all runtime configuration
-   `/status` endpoint returns JSON with relay states and connectivity info

## 9.4 mDNS

-   Registers `_http._tcp` service on port 80
-   Hostname matches OTA hostname (MAC-suffixed)

------------------------------------------------------------------------

# 10. NVS Storage

All persistent configuration is stored in ESP32 NVS (Non-Volatile Storage) using
the Arduino `Preferences` API. Key categories:

-   **WiFi:** SSID, password
-   **MQTT:** host, port, user, password, client ID, base topic, discovery prefix
-   **ESP-NOW:** channel, peer MAC(s), link master key, primary sensor MAC
-   **HVAC state:** mode, fan mode (persisted across reboots)
-   **Relay config:** GPIO pins, inverted flag, interlock delays
-   **OTA:** hostname, password
-   **External services:** PirateWeather API key and ZIP code

------------------------------------------------------------------------

# 11. Partition Scheme

Uses a custom dual-OTA partition layout (`partitions_no_spiffs.csv`):

| Partition | Type    | Offset     | Size      |
|-----------|---------|:----------:|:---------:|
| nvs       | data    | 0x9000     | 20 KB     |
| otadata   | data    | 0xE000     | 8 KB      |
| app0      | ota_0   | 0x10000    | 1984 KB   |
| app1      | ota_1   | 0x200000   | 2048 KB   |

-   No SPIFFS or LittleFS filesystem partition
-   Dual OTA partitions enable safe rollback on failed updates
-   Minimum 4 MB flash required

------------------------------------------------------------------------

# 12. Sensor Architecture

> **Note:** The controller has no local temperature or humidity sensors.
> All environmental data is received from the remote display unit via ESP-NOW
> (primary, low-latency) and/or MQTT (fallback). The controller acts solely as
> a relay driver and HVAC logic engine.

------------------------------------------------------------------------

# 13. PCB Design Requirements

## 13.1 Partitioning

Board divided into zones:

1.  HVAC terminals & relay contacts
2.  Power conversion
3.  MCU & RF area
4.  USB & RS-485 I/O

## 13.2 Creepage & Clearance

-   ≥2mm separation between relay contacts and logic
-   Generous spacing on field wiring nets

## 13.3 RF

-   Respect antenna keepout area
-   No copper under antenna region

------------------------------------------------------------------------

# 14. Safety & Fail-Safe Behavior

-   Relays default open on reset or power loss
-   Hardware pulldowns on relay drivers
-   Brownout detection enabled
-   Supervisor IC recommended

------------------------------------------------------------------------

# 15. Validation Plan

## 15.1 Power Tests

-   AC only
-   USB only
-   AC + USB simultaneously
-   Short 12V output test
-   External 12V backfeed test

## 15.2 Relay Verification

-   Confirm R→W/Y/G/AUX switching
-   Confirm sense inputs reflect state correctly

## 15.3 Environmental Testing

-   0--50°C operation
-   Long wiring noise testing
-   WiFi active during relay switching

------------------------------------------------------------------------

# 16. Component Recommendations

The following components are recommended for the major open selections. Alternatives
are listed for sourcing flexibility — any substitute must meet the electrical
requirements in the referenced sections.

## 16.1 Relays (Section 2.2)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **Omron G5LE-1-DC12** (recommended) | 12V coil, 10A/250VAC contacts, 44mA coil | Widely available, proven HVAC relay |
| _Alt:_ Hongfa HF46F-G/12-HS1T | 12V coil, 10A/250VAC, 36mA coil | Lower coil current, smaller footprint |

## 16.2 Wide-Input Buck: Rectified 24VAC → 12V_SYS (Section 3.2)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **TI LMR16030** (recommended) | 4.3–60V input, 3A output, adjustable | 60V max covers worst-case 32VAC × √2 ≈ 45V peak with margin |
| _Alt:_ TI LM5164 | 6–100V input, 1A, integrated FET | Higher input ceiling, lower current; sufficient if relay coils have dedicated supply |

## 16.3 3.3V Buck: 12V → 3.3V (Section 3.3)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **Diodes Inc AP63203** (recommended) | 4.2–32V input, 2A, SOT-23-6 | Minimal external BOM, excellent for ESP32 loads |
| _Alt:_ TI TLV62569 | 2.5–17V input, 2A, SOT-23-5 | Slightly narrower input range but very compact |

## 16.4 Ideal Diode OR Controller (Section 3.2)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **LTC4412** (recommended) | Low-loss PFET OR controller | One per source rail (24VAC-derived + USB-derived); prevents backfeed |
| _Alt:_ TI TPS2113A | Auto-switching power mux, 3A | Integrated FET, handles switchover logic internally |

## 16.5 USB-UART Bridge (Section 4.1)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **CP2102N** (recommended) | USB 2.0 full-speed, QFN-24 | Already referenced in Section 4.1; native OS drivers, auto-program via DTR/RTS |
| _Alt:_ CH340C | USB 2.0, SOP-16 | Lower cost, requires driver install on some OS versions |

## 16.6 RS-485 Transceiver (Section 5)

| Component | Key Specs | Notes |
|-----------|-----------|-------|
| **TI SN65HVD3082E** (recommended) | 3.3V, half-duplex, fail-safe, SOIC-8 | Proven, low cost, ±15kV ESD on bus pins |
| _Alt:_ TI THVD1500 | 3.3V, half-duplex, ±30kV ESD, fail-safe | Higher ESD rating for harsh environments |

------------------------------------------------------------------------

# 17. Firmware Migration Notes (ESP32 → ESP32-S3)

> **Context:** The current development firmware targets the original ESP32 DevKit
> (`board = esp32dev`). When the custom S3 PCB is ready, the following firmware
> changes are required.

## 17.1 PlatformIO Environment

In `platformio.ini`, change the controller environment board:

```
board = esp32dev          →  board = esp32-s3-devkitc-1
```

Add `framework = arduino` and `platform = espressif32` if not already present.
**Do not** add `board_build.psram` or `board_build.flash_mode` settings — the
controller does not need PSRAM and should use the default flash mode.

## 17.2 Relay GPIO Pin Defaults

In `include/controller/controller_relay_io.h`, update the default pin constants
to match the PCB routing:

| Channel | Current Default (ESP32) | New Default (S3 PCB) |
|---------|:-----------------------:|:--------------------:|
| Heat    | GPIO 32                 | GPIO 4               |
| Cool    | GPIO 33                 | GPIO 5               |
| Fan     | GPIO 25                 | GPIO 6               |
| Spare   | GPIO 26                 | GPIO 7               |

> These match the suggested assignments in Section 2.2. Finalize during PCB
> routing and update both this table and the header file.

## 17.3 Hardware Variant String

In `src/esp32_controller_main.cpp`, update the hardware variant reported in
telemetry and discovery payloads:

```cpp
cfg.hardware_variant = "ESP32";    →  cfg.hardware_variant = "ESP32-S3";
```

## 17.4 ESP32-S3 Module Selection

| Module | Flash | PSRAM | Notes |
|--------|:-----:|:-----:|-------|
| **ESP32-S3-WROOM-1-N8** (recommended) | 8 MB | None | Preferred — extra flash gives OTA headroom with dual partitions |
| ESP32-S3-WROOM-1-N4 (minimum) | 4 MB | None | Meets current partition table requirements but tight for future growth |

Do not select a PSRAM variant (N4R2, N8R8, etc.) — the controller firmware does
not use PSRAM and the additional pins/routing are unnecessary.

------------------------------------------------------------------------

# End of Document
