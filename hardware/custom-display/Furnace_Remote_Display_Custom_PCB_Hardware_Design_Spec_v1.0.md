# Furnace Remote Display Custom PCB
## Hardware Design Specification (patterned after ESP32-8048S043C_I class boards)
**Version:** 1.0  
**Date:** 2026-03-02  

---

## 1. Purpose

Design a **remote display PCB** for the furnace system that provides a local UI (touchscreen) and connects to the furnace controller over **Wi‑Fi** (no wired comms required). The design should be **patterned after** the commonly available ESP32‑S3 + 4.3" capacitive touch display boards currently in use (DIYmalls / “ESP32‑8048S043C_I” style). These boards are typically described as an ESP32‑S3 development board combined with a **4.3" 800×480 IPS capacitive touch display**. citeturn0search0turn1search2

This custom PCB must additionally include:
- **12V input** (from the furnace controller) with an **onboard DC‑DC converter**
- **AHT20 temperature/humidity sensor** placed and thermally isolated to achieve accurate ambient readings (not biased by the display/backlight or regulators)

---

## 2. High-Level Requirements

### 2.1 Core Functionality
- ESP32‑S3 MCU module (Wi‑Fi/BLE) drives the UI and communicates to the controller via Wi‑Fi.
- Integrated **4.3" 800×480** TFT with **capacitive touch** (IPS preferred). citeturn0search0turn1search2
- Optional removable storage (TF / microSD) similar to existing boards (for assets, logging, etc.). citeturn0search0turn1search2

### 2.2 Power
- Primary power input: **12VDC** (nominal) from furnace controller.
- The board generates all required rails locally:
  - **3.3V** for ESP32‑S3 + sensors + touch controller
  - **(Optional)** 5V rail if required by the display/backlight or peripherals (implementation dependent)
- Provide robust input protection and stable operation during Wi‑Fi current bursts and backlight switching.

### 2.3 Sensor Accuracy
- Include **AHT20** temperature/humidity sensor (I²C).
- Typical accuracy targets (at 25°C):
  - Humidity: **±2% RH**
  - Temperature: **±0.3°C** citeturn1search5turn1search1
- I²C address: **0x38** citeturn1search5
- Mechanical + PCB layout must minimize self-heating influence.

---

## 3. System Block Diagram (Logical)

**12V IN** → Protection (fuse/eFuse + TVS + reverse protection) → DC/DC → 3.3V (and optional 5V)  
3.3V → ESP32‑S3 module + touch controller + AHT20  
ESP32‑S3 → LCD interface + backlight control + touch I²C/INT/RST  
Optional TF/microSD via SPI or SDMMC (depending on chosen module/display board architecture)

---

## 4. Subsystems

### 4.1 MCU Subsystem (ESP32‑S3)
**Recommended approach:** Use an ESP32‑S3 module (e.g., WROOM class) for RF predictability and easier manufacturing.

**Minimum requirements:**
- ≥16MB flash and ≥8MB PSRAM strongly recommended for rich UIs (matches typical “panel” boards). citeturn1search2
- Access to:
  - LCD data/control pins (per display interface choice)
  - Touch I²C + INT + RST (as needed)
  - TF/microSD interface (optional)
  - UART programming/debug, EN/BOOT

**Debug/Programming:**
- Provide USB‑C for development (power + USB‑serial) *or* a dedicated UART header.
  - If USB‑C is included: ESD protection on D+/D‑ and correct CC resistors for UFP device mode.
  - Auto-program (DTR/RTS → EN/IO0) recommended.

> Note: The existing “panel” ecosystems often use known pin maps for RGB LCD + capacitive touch; this spec does not lock a pin map unless your firmware stack depends on a specific community mapping. If you want to preserve compatibility with ESP32‑8048S043C class pinouts, use a pin‑mapping appendix in the implementation package (schematic notes). citeturn1search7turn1search2

---

### 4.2 Display Subsystem (4.3" TFT + Capacitive Touch)

**Display target (pattern reference):**
- 4.3" TFT, **800×480**, IPS preferred, 16-bit color.
- Capacitive touch controller (commonly I²C). citeturn0search0turn1search2

**Interface options (choose based on panel module availability):**
- **RGB (parallel)**: best performance for 800×480 + LVGL; more pins; careful routing.
- **SPI**: fewer pins but often slower; may limit UI smoothness at 800×480.
- Many 4.3" “panel” boards in this class use RGB to achieve good performance (verify for chosen panel). citeturn1search2turn1search7

**Backlight:**
- Provide backlight power path per panel requirements:
  - If panel uses LED strings requiring boost: integrate/allow a boost driver.
  - If panel accepts a supply rail: include a load switch + PWM dimming.
- PWM dimming:
  - PWM frequency out of audible range (e.g., >20kHz).
  - Avoid coupling noise into AHT20 area (see placement section).

---

### 4.3 Power Subsystem (12V In → Local Rails)

**Input:**
- 12V nominal from controller (design input range: **9–15V** minimum; consider 8–18V if cable drop or supply variance is possible).

**Protection (required):**
- Reverse polarity protection (ideal diode or Schottky).
- TVS diode on 12V input.
- Current limiting:
  - eFuse / power switch preferred (inrush + short protection), or
  - resettable fuse (PTC) + design margin.

**Conversion:**
- Primary requirement: **3.3V rail** capable of:
  - ESP32‑S3 Wi‑Fi peaks
  - display/touch load
  - sensor load
- Recommended design budget:
  - 3.3V regulator: **≥1.0A peak** capability, low ripple.
- Optional 5V rail if required (depends on panel/backlight architecture).

**Power integrity:**
- Bulk capacitance near DC/DC output(s) and near ESP32 module.
- Separate “noisy” power routing (DC/DC, backlight) from “quiet” sensor/ADC/I²C routing.

---

### 4.4 AHT20 Sensor Subsystem

**Electrical:**
- I²C at 3.3V.
- Pullups sized for bus capacitance; ensure stable rise time.
- I²C address fixed at **0x38**. citeturn1search5
- AHT20 operating voltage range: 2.0–5.5V (supports 3.3V). citeturn1search5turn1search1

**Accuracy target:**
- Temperature ±0.3°C typical; humidity ±2% RH typical. citeturn1search5turn1search1

---

## 5. Mechanical / Industrial Design Constraints

### 5.1 Form Factor
- Match mounting hole pattern and connector edge placement of the existing board **as closely as practical** to allow drop-in enclosure reuse (if desired).
- Ensure the TFT is front‑mountable and flush with bezel/faceplate.

### 5.2 Connectors
Minimum:
- **12V IN** connector (locking preferred, e.g., JST‑VH / MicroFit / screw terminal).
Optional:
- USB‑C (debug/programming)
- TF/microSD
- Expansion header (I²C/UART/GPIO) for future sensors

---

## 6. Temperature Sensor Placement & Thermal Isolation (Critical)

Goal: Ensure AHT20 measures **ambient air temperature** near the display location, not board self-heating.

### 6.1 Placement Rules
1. Place AHT20 **at the board edge** with access to airflow.
2. Keep distance from heat sources:
   - DC/DC inductor and switching IC
   - backlight driver/boost components
   - ESP32 module (RF + MCU heat)
   - any linear regulators
3. Recommended separation:
   - **≥25 mm** from DC/DC power stage and inductors (more if possible)
   - **≥15–20 mm** from display backlight power components
4. Put the sensor on the **opposite side** of the PCB from the main heat sources if enclosure geometry allows.

### 6.2 Thermal Isolation Techniques (use multiple)
- Add a **thermal isolation slot** or perforations in PCB around the sensor “peninsula” to reduce conduction from the main board.
- Avoid copper pours under/around the sensor:
  - no large ground pour directly under the sensor
  - limit copper to required pads and short traces
- Use thin traces to the sensor and avoid thermal vias nearby.
- If the enclosure allows, add **vent openings** near the sensor area to encourage airflow.
- Keep the sensor away from direct sunlight / heated bezel regions (if wall-mounted).

### 6.3 Electrical Noise Considerations
- Route I²C lines away from:
  - backlight PWM traces
  - DC/DC switch node
- Add local decoupling near AHT20 per datasheet guidance.

### 6.4 Validation Requirement
During prototype validation, verify sensor self-heating impact by:
- Measuring reported temperature with screen at min brightness vs max brightness.
- Measuring with Wi‑Fi idle vs active (continuous throughput).
- Target: <0.3°C shift attributable to board heating under typical use.

---

## 7. PCB Layout Requirements

### 7.1 Zone Partitioning
Partition the board into:
1. **Power zone** (12V input + protection + DC/DC + backlight power)
2. **Logic zone** (ESP32‑S3 + clocks + flash/PSRAM module)
3. **Display/touch connector zone** (short, impedance‑aware routing)
4. **Sensor zone** (thermally isolated AHT20 area)

### 7.2 RF
- Respect ESP32 module antenna keepout (no copper, no high-speed traces under antenna).

### 7.3 EMC/ESD
- ESD diodes on external connectors (12V in, USB, any expansion headers).
- Keep high dV/dt switch nodes small and away from edges/cables.

---

## 8. Firmware/Hardware Support Features

- Backlight PWM control pin (with a default safe state).
- Touch controller interrupt pin (optional but recommended for responsive UI).
- Boot/Reset buttons or accessible pads.
- At least one status LED (power or user activity).

---

## 9. Power Budget (Planning Numbers)

These should be refined once the exact panel/backlight is chosen.

- ESP32‑S3 Wi‑Fi peaks: plan for **500–700 mA** transient capability on 3.3V rail.
- Touch controller + AHT20: small (<20 mA typical, sensor typically sub‑mA average depending on sampling). citeturn1search5
- Backlight: varies widely; design must support the chosen panel’s backlight current with margin.

**Recommendation:** Size 12V input path and regulator(s) for **≥1.5 A peak** system draw unless your selected panel is known to be lower.

---

## 10. Acceptance Tests

### 10.1 Power
- Input range test: 9V, 12V, 15V at min/max backlight.
- Brownout behavior: verify no file corruption and clean resets.
- Inrush/short protection: short 3.3V briefly (bench test) and validate protection behavior (if eFuse used).

### 10.2 Display/Touch
- Full-screen refresh rate meets UI requirements (LVGL target).
- Touch accuracy and latency acceptable across temperature range.

### 10.3 Sensor Accuracy
- Compare AHT20 readings to a reference sensor in a stable environment.
- Run sustained test with backlight at max and Wi‑Fi busy; validate thermal isolation goal (<0.3°C bias under typical operation).

### 10.4 ESD
- Basic ESD robustness checks at 12V input and USB connector (as practical for your lab setup).

---

## 11. Open Engineering Decisions (to finalize in schematic phase)

1. Exact display panel + touch controller selection (interface: RGB vs SPI).
2. Whether to include 5V rail (depends on panel/backlight needs).
3. USB‑C inclusion level (debug-only vs also field-service port).
4. SD card interface choice (SPI vs SDMMC) and whether it’s required.
5. Exact AHT20 thermal isolation geometry (slots/perforations) based on enclosure.

---

## Appendix A: Source References (Pattern Board + Sensor Specs)

- DIYmalls / ESP32-S3 4.3" display board description (800×480 IPS, capacitive touch, TF support). citeturn0search0  
- Panel class description for ESP32‑8048S043C: ESP32‑S3 + 800×480 + touch + 16MB flash/8MB PSRAM. citeturn1search2  
- AHT20 datasheet + typical accuracy and package info. citeturn1search1turn1search8  
- AHT20 operating voltage, address 0x38, and accuracy summary. citeturn1search5  

