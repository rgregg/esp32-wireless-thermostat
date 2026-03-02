# Furnace Controller Custom PCB

## Hardware Design Specification

**Version:** 1.0\
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

# 5. RS-485 Interface

-   RS-485 transceiver
-   A/B/GND connector (3-pin minimum)
-   Optional 120Ω termination resistor (jumper controlled)
-   Optional bias resistors footprint
-   Controlled via ESP32 UART + DE/RE GPIO

------------------------------------------------------------------------

# 6. Call-Line Sense Inputs

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

# 7. PCB Design Requirements

## 7.1 Partitioning

Board divided into zones:

1.  HVAC terminals & relay contacts
2.  Power conversion
3.  MCU & RF area
4.  USB & RS-485 I/O

## 7.2 Creepage & Clearance

-   ≥2mm separation between relay contacts and logic
-   Generous spacing on field wiring nets

## 7.3 RF

-   Respect antenna keepout area
-   No copper under antenna region

------------------------------------------------------------------------

# 8. Safety & Fail-Safe Behavior

-   Relays default open on reset or power loss
-   Hardware pulldowns on relay drivers
-   Brownout detection enabled
-   Supervisor IC recommended

------------------------------------------------------------------------

# 9. Validation Plan

## 9.1 Power Tests

-   AC only
-   USB only
-   AC + USB simultaneously
-   Short 12V output test
-   External 12V backfeed test

## 9.2 Relay Verification

-   Confirm R→W/Y/G/AUX switching
-   Confirm sense inputs reflect state correctly

## 9.3 Environmental Testing

-   0--50°C operation
-   Long wiring noise testing
-   WiFi active during relay switching

------------------------------------------------------------------------

# 10. Open Engineering Decisions

-   Final relay part selection
-   Exact buck regulator components
-   Ideal diode OR controller selection
-   USB-UART bridge selection
-   RS-485 transceiver selection

------------------------------------------------------------------------

# End of Document
