# Furnace Remote Display Custom PCB

## Hardware Design Specification (patterned after ESP32-8048S043C_I class boards)

**Version:** 1.3\
**Date:** 2026-03-02  

_Last updated:_ 2026-03-02  

_Last updated:_ 2026-03-02

------------------------------------------------------------------------

## 1. Purpose

Design a remote display PCB for the furnace system that provides a local
UI (touchscreen) and connects to the furnace controller over Wi‑Fi.

This custom PCB includes:

-   ESP32-S3 MCU
-   4.3" 800×480 capacitive touch display
-   12V input with onboard DC-DC conversion
-   AHT20 temperature/humidity sensor with thermally isolated placement
-   Mechanical and connector layout optimized for wall-mounted
    installation

------------------------------------------------------------------------

## 2. Mechanical & Physical Layout Requirements

### 2.1 Connector Placement (Critical)

-   **12V power connector MUST be located on the back side of the PCB**
-   No power wiring should exit from the front or side edges
-   Connector should support clean wall mounting with rear cable entry
-   Recommended: locking connector (MicroFit / JST-VH / pluggable
    terminal)

### 2.2 Temperature Sensor Placement (Critical)

The AHT20 temperature sensing area:

-   MUST be located on the **front side of the PCB**
-   Must be positioned either:
    -   Left of the display active area, OR
    -   Right of the display active area
-   Must not be located behind the LCD or near backlight driver
    components
-   Must have exposure to ambient air (vented bezel or airflow path)

------------------------------------------------------------------------

## 3. Thermal Isolation Requirements

To ensure accurate ambient readings:

### 3.1 Placement Separation

Minimum spacing from heat sources:

-   ≥25mm from DC/DC converters and inductors
-   ≥20mm from ESP32 module
-   ≥20mm from backlight driver components
-   Not directly above copper pours tied to high current nets

### 3.2 PCB Thermal Isolation Techniques

-   Create a "sensor peninsula" area using:
    -   Routed isolation slot OR perforation pattern
-   Minimize copper beneath sensor
-   Avoid thermal vias near sensor
-   Use narrow traces to sensor
-   Keep sensor on edge for airflow exposure

### 3.3 Validation Requirement

Under maximum backlight brightness and active Wi-Fi:

-   Temperature drift due to board self-heating must be \<0.3°C
-   Verify with controlled comparison testing

------------------------------------------------------------------------

## 4. Power Architecture

### 4.1 12V Input

-   Input range: 9--15V minimum (design target 8--18V tolerance)
-   Reverse polarity protection
-   TVS protection
-   eFuse or resettable fuse protection

### 4.2 DC-DC Conversion

-   12V → 3.3V primary rail
-   3.3V rail must support ≥1A peak current
-   Proper bulk capacitance near ESP32 and display interface

------------------------------------------------------------------------

## 5. Display Subsystem

-   4.3" IPS TFT
-   800×480 resolution
-   Capacitive touch controller via I²C
-   PWM-controlled backlight (\>20kHz)

------------------------------------------------------------------------

## 6. AHT20 Sensor Subsystem

-   I²C address: 0x38
-   Powered at 3.3V
-   Humidity accuracy: ±2% RH typical
-   Temperature accuracy: ±0.3°C typical

------------------------------------------------------------------------

## 7. PCB Partitioning

Board zones:

1.  Power conversion zone (rear region preferred)
2.  MCU + logic zone
3.  Display interface zone
4.  Sensor zone (front edge left or right)

------------------------------------------------------------------------

## 8. Acceptance Tests

-   12V input variation test
-   Backlight max brightness thermal test
-   Wi-Fi sustained throughput thermal test
-   Sensor drift verification
-   ESD robustness at power connector

------------------------------------------------------------------------


---

## 9. Pinout Compatibility With Reference Dev Board (Required)

### 9.0 Display & Touch Hardware Compatibility (Required)

In addition to matching GPIO assignments, the custom PCB **MUST use the same display and touchscreen controller hardware** as the reference ESP32-8048S043C style board to ensure driver-level compatibility without firmware changes.

#### 9.0.1 LCD Panel / Display Engine (Required)
- Use a **4.3" 800×480, 16-bit color RGB parallel panel** that is **based on the ST7262** display engine, matching the reference board. citeturn2view0
- Interface requirements (must match reference expectations): **4 control + 16 data** signals for the ESP32-S3 “parallel RGB” mode with DMA. citeturn2view0
- Color depth: **RGB565 (5-6-5)** operation, as used by the reference panel configuration. citeturn2view0
- Backlight control must be compatible with the reference approach:
  - Provide a backlight enable/PWM control net on **GPIO02** (LIGHT pin), matching reference configuration. citeturn2view0

> Implementation note: Many vendor listings are inconsistent about “driver IC” naming. For compatibility, lock the panel selection to a known-good **ST7262-based 4.3" 800×480 RGB panel** used in ESP32-8048S043C boards, or procure the same panel/FPC assembly from the same supply chain as the reference board.

#### 9.0.2 Touchscreen Controller (Required)
- Use the **GT911** capacitive touch controller, matching the reference board. citeturn2view0
- Electrical requirements:
  - I²C address **0x5D** citeturn2view0
  - I²C bus pins: SDA=GPIO19, SCL=GPIO20 (already required by pin map) citeturn2view0
  - Reset pin: **GPIO38** (already required by pin map) citeturn2view0
- Interrupt behavior:
  - Reference behavior uses polling (interrupt pin not connected by default). Preserve this by default, but keep the optional strap to connect INT to GPIO18 as specified earlier.

#### 9.0.3 Procurement / BOM Control (Required)
Hardware engineering must treat the following as **locked compatibility items**:
- LCD panel: ST7262-based 4.3" 800×480 RGB (16-bit) panel + matching FPC pinout
- Touch controller: GT911 (I²C @ 0x5D)

Any substitutions must be explicitly reviewed for:
- Matching interface type (RGB parallel vs SPI/MIPI)
- Matching I²C touch protocol and addressability
- Matching panel timing and porch/pulse requirements
- Firmware driver compatibility with existing LVGL/Arduino_GFX/LovyanGFX configuration



To preserve firmware compatibility (LVGL display drivers, touch drivers, bootloader tooling, community examples, etc.), the custom PCB **MUST match the reference board’s GPIO assignments** for:

- LCD RGB data lines and sync/control signals
- Touch controller I²C and reset (and optional interrupt behavior)
- TF/microSD interface (if included)
- Boot/Reset behavior (EN / IO0)

### 9.1 Reference Board Used for Pin Mapping

The pin mapping below is derived from the publicly shared **ESP32-8048S043** board layout/schematic image (macsbug, dated 2022-10-18).  
Reference: https://macsbug.wordpress.com/wp-content/uploads/2022/10/4280s043_layout-2.pdf

> Note: This is the common “Sunton / ESP32-8048S043C” style pin map that many example projects assume.

### 9.2 LCD (RGB) Signal → ESP32-S3 GPIO Map

**LCD connector signals (RGB + sync) must be wired to these ESP32-S3 GPIOs:**

| LCD Signal | ESP32-S3 GPIO |
|---|---:|
| DE | GPIO40 |
| VSYNC | GPIO41 |
| HSYNC | GPIO39 |
| DCLK | GPIO42 |
| DISP (Display Enable) | **R16 / board-defined** (see note below) |
| B7 | GPIO1 |
| B6 | GPIO9 |
| B5 | GPIO46 |
| B4 | GPIO3 |
| B3 | GPIO8 |
| G7 | GPIO4 |
| G6 | GPIO16 |
| G5 | GPIO15 |
| G4 | GPIO7 |
| G3 | GPIO6 |
| G2 | GPIO5 |
| R7 | GPIO14 |
| R6 | GPIO21 |
| R5 | GPIO47 |
| R4 | GPIO48 |
| R3 | GPIO45 |
| VDD | 3.3V |
| VLED+ / LEDA | Backlight anode supply (per panel) |
| VLED- / LEDK | Backlight cathode / return (per panel) |
| GND pins | GND |

**Important notes:**
- Several RGB bits on the reference board are tied to GND (B2/B1/B0, G1/G0, R2/R1/R0). This implies the panel is being driven in a reduced-bit mode (commonly 16-bit 565 uses a subset of the RGB lines). **Match the reference wiring**, even if your panel could support more bits, to keep software compatibility.
- The **DISP** line on the reference layout is not clearly annotated as a direct GPIO in the PDF (it is marked via “R16”). For compatibility:
  - Implement a **DISP control net** that matches the existing board’s behavior (always enabled OR controlled exactly as reference if you confirm the GPIO in your current firmware).
  - Add a test pad and allow rework (0Ω link footprint) so DISP can be strapped to a GPIO later if needed.

### 9.3 Touch Controller (GT911) → ESP32-S3 GPIO Map

The reference board uses a capacitive touch controller (commonly GT911) on an FPC.

| Touch Signal | ESP32-S3 GPIO / Net |
|---|---:|
| SDA | GPIO19 |
| SCL | GPIO20 |
| RST | GPIO38 |
| INT | **Not connected by default** (“TP-INT” test point, marked open). Optional strap to GPIO18. |
| VDD | 3.3V (or panel-specified) |
| GND | GND |

**Compatibility requirement:**
- Wire touch I²C and RST exactly as above.
- For INT behavior:
  - Provide the same default as reference (INT not populated/connected), **AND**
  - Provide an optional population path (0Ω resistor / solder jumper) to connect INT to **GPIO18** for interrupt-driven touch (many users mod the board to reduce CPU usage).

### 9.4 TF / microSD (Optional) → ESP32-S3 GPIO Map

If TF/microSD is included, match the reference wiring:

| SD Function | ESP32-S3 GPIO |
|---|---:|
| CS | GPIO10 |
| MOSI / CMD | GPIO11 |
| SCLK / CLK | GPIO12 |
| MISO / DAT0 | GPIO13 |
| (Card Detect / other) | Follow reference if implemented; otherwise expose footprint/test pads. |

### 9.5 Boot / Reset & Programming

- BOOT button: GPIO0 (IO0) to GND (standard ESP32 boot strap)
- RESET button: EN (chip enable / reset)
- If using USB-UART + auto-program:
  - DTR/RTS should drive IO0/EN in the standard ESP32 auto-reset topology.
- Preserve the “works like the dev board” experience: **USB-C + flashing via ESP tools**.

### 9.6 Implementation Guidance

To ensure the team matches the pin map correctly:
- Include the above table directly in the schematic notes.
- Label nets with both roles, e.g., `LCD_DE_GPIO40`, `TP_SCL_GPIO20`, etc.
- Add test pads for every critical interface line (DCLK/HSYNC/VSYNC/DE, touch SDA/SCL/RST/INT, SD CS/CLK/MOSI/MISO).


# End of Document
