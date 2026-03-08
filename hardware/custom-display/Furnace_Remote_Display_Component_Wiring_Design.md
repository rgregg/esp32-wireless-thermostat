# Furnace Remote Display — Component Wiring Design

**Board:** Furnace Remote Display Custom PCB v1.4
**MCU:** ESP32-S3-WROOM-1 (N16R8)
**Companion Document:** `Furnace_Remote_Display_Custom_PCB_Hardware_Design_Spec_v1.4.md`

This document specifies the complete wiring for every subsystem: component values, pin connections, and design notes. It serves as the direct input for schematic capture.

---

## 1. 12V Input Protection

### Schematic

```
+12V_IN ── PTC ──┬── Q_rpol(S) ── Q_rpol(D) ──┬── TVS ──┬── C_in ──┬── 12V_INT
                  │                              │         │          │
                  │   Gate divider               │         │      C_bulk
                  │   (turns off P-FET           │         │          │
                  │    if polarity reversed)     GND      GND        GND
                  │
GND_IN ──────────GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| F1 | Resettable PTC | 1812L100/12MR (1A hold) | Protects against overcurrent |
| Q1 | Reverse polarity P-FET | Si2301 (SOT-23, −2.3A, 20V) | Gate to GND via 100kΩ; source to input |
| R1 | Gate pull-down | 100kΩ (0402) | Pulls gate low (turns on P-FET) when polarity correct |
| D1 | TVS | SMAJ16A (16V standoff, 25.6V clamp, SMA) | Clamps cable transients |
| C1 | Input ceramic | 10µF / 25V (X5R, 1210) | |
| C2 | Input electrolytic | 47µF / 25V | Bulk storage for transient loads |
| J1 | Connector | JST-VH 2-pin or MicroFit 3.0 2-pin | Locking, polarized, on PCB back side |

### Pin Connections

| Connector Pin | Net |
|--------------|-----|
| Pin 1 | +12V_IN |
| Pin 2 | GND |

### Reverse Polarity Protection Detail

```
+12V_IN ── PTC ── Si2301 Source
                   Si2301 Gate ── R1 (100kΩ) ── GND
                   Si2301 Drain ── 12V (protected)
```

- When polarity is correct: Vgs < 0 (gate at GND, source at +12V) → P-FET ON
- When polarity is reversed: Vgs > 0 → P-FET OFF, blocking reverse current

### Design Notes

- 1A PTC sized for display worst-case: LCD + backlight (~200mA) + ESP32 (~300mA) + margin
- SMAJ16A matches controller output protection for consistent clamping
- Connector on PCB back side for clean wall-mount installation
- **Simpler alternative:** Replace P-FET with series Schottky SS34 (3A, 40V) — ~0.5V drop but fewer components

---

## 2. 3.3V Buck (AP63203)

### Schematic

```
12V_INT ── C_in ──┬── VIN ┌──────────┐ SW ── L1 ──┬── 3V3
                   │       │ AP63203  │             │
                   │  BST ─┤ (3.3V)  ├─ GND        C_out
                   │  C_bst│          │             │
                   │       └──────────┘             GND
                   GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U1 | 3.3V buck | Diodes Inc AP63203WU-7 (SOT-23-6, fixed 3.3V) | 2A, internal compensation |
| C3 | Input ceramic | 10µF / 25V (X5R, 0805) | Close to VIN pin |
| C4 | Bootstrap | 0.1µF / 16V ceramic (0402) | Between BST and SW |
| L1 | Inductor | 3.3µH, ≥2A saturation (Würth 744774033) | Shielded |
| C5, C6 | Output ceramic | 2× 22µF / 10V (X5R, 0805) | Low ESR |

### Pin Connections

| AP63203 Pin | Connection |
|-------------|------------|
| VIN | 12V_INT (via C3) |
| BST | C4 to SW |
| SW | L1 → output caps → 3V3 net |
| FB | Internal (fixed 3.3V) |
| EN | Float (internal pull-up, always on) |
| GND | GND |

### Design Notes

- Identical circuit to controller board Section 5
- Fixed 3.3V — no feedback divider needed
- Place inductor and caps within 5mm of IC; ground via array under exposed pad
- **Thermal note:** Place ≥25mm from temperature sensor to minimize self-heating effect

---

## 3. Backlight Driver

### Schematic

```
                    12V_INT (or panel-specified LEDA supply)
                       │
                     LEDA (Backlight anode, via FPC)
                       │
                   [LCD backlight LED string]
                       │
                     LEDK (Backlight cathode, via FPC)
                       │
                    Drain(Q2)
                       │
GPIO 2 ── R_ser ── Gate(Q2)    N-FET
                       │
                    R_pd
                       │
                    Source(Q2) ── GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| Q2 | N-FET (backlight switch) | AO3400A (SOT-23, 30V, Vgs(th) ~1.4V) | PWM switching at 800Hz |
| R2 | Gate series | 100Ω (0402) | Limits gate ringing |
| R3 | Gate pull-down | 10kΩ (0402) | Backlight OFF during boot |
| R4 | Backlight current limit | Size per panel spec | See design notes |

### Pin Connections

| Signal | Connection |
|--------|-----------|
| GPIO 2 | R2 (100Ω) → Q2 Gate |
| Q2 Drain | LEDK (backlight cathode via FPC) |
| Q2 Source | GND |
| LEDA | 12V_INT or panel-specified supply (via FPC) |

### Design Notes

- Firmware uses **800Hz PWM**, 8-bit resolution (0–255 duty cycle) via LEDC peripheral
- Backlight held off for 400ms after display init to avoid showing garbage frames
- Default active brightness: 100%; screensaver brightness: 16%
- 10kΩ pull-down ensures backlight OFF during ESP32 boot/reset
- **Panel-specific:** If the 4.3" panel's LED string forward voltage is known (e.g., 3S × 3.2V = 9.6V), drive from 12V with series resistor R4 = (12V − Vf_string) / I_backlight. If panel requires a boost or constant-current driver, add accordingly.
- At 800Hz, FET switching is trivial — no special gate driver needed
- If audible coil whine occurs, firmware frequency can be increased

---

## 4. LCD RGB Interface

### Signal Connections

| LCD Signal | ESP32-S3 GPIO | Type | Notes |
|-----------|:------------:|------|-------|
| DE | GPIO 40 | Output | Data Enable |
| VSYNC | GPIO 41 | Output | Vertical Sync |
| HSYNC | GPIO 39 | Output | Horizontal Sync |
| DCLK | GPIO 42 | Output | Pixel Clock |
| B7 | GPIO 1 | Output | Blue bit 7 (MSB) |
| B6 | GPIO 9 | Output | Blue bit 6 |
| B5 | GPIO 46 | Output | Blue bit 5 |
| B4 | GPIO 3 | Output | Blue bit 4 |
| B3 | GPIO 8 | Output | Blue bit 3 (LSB for blue) |
| G7 | GPIO 4 | Output | Green bit 7 (MSB) |
| G6 | GPIO 16 | Output | Green bit 6 |
| G5 | GPIO 15 | Output | Green bit 5 |
| G4 | GPIO 7 | Output | Green bit 4 |
| G3 | GPIO 6 | Output | Green bit 3 |
| G2 | GPIO 5 | Output | Green bit 2 (LSB for green) |
| R7 | GPIO 14 | Output | Red bit 7 (MSB) |
| R6 | GPIO 21 | Output | Red bit 6 |
| R5 | GPIO 47 | Output | Red bit 5 |
| R4 | GPIO 48 | Output | Red bit 4 |
| R3 | GPIO 45 | Output | Red bit 3 (LSB for red) |

### Unused RGB LSBs

| LCD Signal | Connection | Notes |
|-----------|-----------|-------|
| B2, B1, B0 | 10kΩ pull-down to GND each | Unused — RGB565 mode |
| G1, G0 | 10kΩ pull-down to GND each | Unused — RGB565 mode |
| R2, R1, R0 | 10kΩ pull-down to GND each | Unused — RGB565 mode |

### DISP (Display Enable)

| Signal | Connection | Notes |
|--------|-----------|-------|
| DISP | 10kΩ pull-up to 3V3 | Permanently enabled |
| DISP test pad | 0Ω footprint to GPIO test pad | Optional: for future firmware control |

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| J2 | FPC connector | 40-pin FPC, 0.5mm pitch | Match panel pinout |
| R5–R12 | LSB pull-downs | 8× 10kΩ (0402) | B2–B0, G1–G0, R2–R0 |
| R13 | DISP pull-up | 10kΩ (0402) | DISP to 3V3 |

### Design Notes

- 21 GPIOs for RGB565 parallel interface (16 data + 4 sync + 1 enable)
- Panel: 4.3" IPS TFT, 800×480, ST7262 display engine
- No external components needed on data/sync lines — direct ESP32-S3 GPIO to FPC
- Firmware sets `disp_gpio_num = -1` (DISP not GPIO-controlled) — hardware pull-up keeps display always enabled
- RGB signal routing: keep traces matched length (±5mm) for DCLK <25MHz operation

---

## 5. Touch Controller (GT911)

### Schematic

```
3V3 ── R_sda (4.7kΩ) ──┬── SDA ── GT911 SDA
3V3 ── R_scl (4.7kΩ) ──┬── SCL ── GT911 SCL
                         │
ESP32 GPIO 19 ──────── SDA (I²C Bus 0)
ESP32 GPIO 20 ──────── SCL (I²C Bus 0)
ESP32 GPIO 38 ──────── GT911 RST (via 10kΩ pull-up to 3V3)
                        GT911 INT ── 0Ω pad (DNP) ── GPIO 22
                        GT911 VDD ── 3V3 (via C_dec)
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U2 | Touch controller | GT911 (on FPC / panel module) | I²C address 0x5D |
| R14 | SDA pull-up | 4.7kΩ (0402) | Bus 0 SDA to 3V3 |
| R15 | SCL pull-up | 4.7kΩ (0402) | Bus 0 SCL to 3V3 |
| R16 | RST pull-up | 10kΩ (0402) | Active-low reset to 3V3 |
| R17 | INT pull-up (GPIO side) | 10kΩ (0402) | On GPIO 22 side of 0Ω pad |
| R18 | INT 0Ω link | 0Ω (0402) — **DNP** (Do Not Populate) | Optional: connects INT to GPIO 22 |
| C7 | VDD decoupling | 100nF / 10V (0402) | At GT911 VDD pin |

### Pin Connections

| GT911 Pin | Connection |
|-----------|-----------|
| SDA | GPIO 19 (I²C Bus 0 SDA) — via 4.7kΩ pull-up to 3V3 |
| SCL | GPIO 20 (I²C Bus 0 SCL) — via 4.7kΩ pull-up to 3V3 |
| RST | GPIO 38 — via 10kΩ pull-up to 3V3 |
| INT | 0Ω pad (DNP) to GPIO 22 — default: not connected (polling mode) |
| VDD | 3V3 (via C7) |
| GND | GND |

### Design Notes

- I²C Bus 0 at 400kHz — dedicated to touch controller only
- Default behavior: polling mode (INT not connected), matching reference board
- If interrupt-driven touch desired: populate R18 (0Ω) to connect INT to GPIO 22
- **GPIO 22 chosen** because GPIO 18 (referenced in some docs as INT strap option) is now used for sensor I²C SDA
- GT911 I²C address 0x5D is set by INT pin state during reset — ensure INT is LOW during RST rising edge (pull-down on INT, or leave floating with GT911's internal pull)

---

## 6. Sensor I²C (AHT20 / Si7021)

### Schematic

```
3V3 ── R_sda (4.7kΩ) ──┬── SDA ── Sensor SDA
3V3 ── R_scl (4.7kΩ) ──┬── SCL ── Sensor SCL
                         │
ESP32 GPIO 18 ──────── SDA (I²C Bus 1)
ESP32 GPIO 17 ──────── SCL (I²C Bus 1)
                        Sensor VDD ── 3V3 (via C_dec)
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U3 | Temp/humidity sensor (primary) | AHT20 (DFN-6, 3×3mm) | I²C address 0x38 |
| U3_alt | Temp/humidity sensor (fallback) | Si7021 (DFN-6, 3×3mm) | I²C address 0x40 |
| R19 | SDA pull-up | 4.7kΩ (0402) | Bus 1 SDA to 3V3 |
| R20 | SCL pull-up | 4.7kΩ (0402) | Bus 1 SCL to 3V3 |
| C8 | Sensor VDD decoupling | 100nF / 10V (0402) | At sensor VDD pin |

### Pin Connections

| Sensor Pin | Connection |
|-----------|-----------|
| SDA | GPIO 18 (I²C Bus 1 SDA) — via 4.7kΩ pull-up to 3V3 |
| SCL | GPIO 17 (I²C Bus 1 SCL) — via 4.7kΩ pull-up to 3V3 |
| VDD | 3V3 (via C8) |
| GND | GND |

### Dual Footprint

- AHT20 and Si7021 share the same DFN-6 package and compatible pinout
- Provide a single footprint that accommodates either sensor
- Firmware auto-detects: probes AHT20 (0x38) first with ghost-device validation, then falls back to Si7021 (0x40)

### Thermal Isolation (Critical)

| Requirement | Specification |
|------------|--------------|
| Distance from DC-DC inductors | ≥25mm |
| Distance from ESP32 module | ≥20mm |
| Distance from backlight driver | ≥20mm |
| Copper beneath sensor | Minimized — no ground pour |
| PCB technique | Sensor peninsula with isolation slot or perforations |
| Trace routing | Narrow traces only (minimize thermal conduction) |
| Placement | Board edge for airflow exposure |
| Validation target | Self-heating drift <0.3°C at max backlight + active Wi-Fi |

### Design Notes

- I²C Bus 1 at 100kHz — dedicated to sensor only
- Separate bus from touch (Bus 0) avoids address conflicts and allows independent clock speeds
- AHT20 accuracy: ±0.3°C, ±2% RH; Si7021 accuracy: ±0.4°C, ±3% RH
- Thermal isolation is the most critical layout constraint for this subsystem

---

## 7. SD Card (Optional)

### Schematic

```
ESP32 GPIO 10 ── R_cs (10kΩ pull-up to 3V3) ── SD CS
ESP32 GPIO 11 ──────────────────────────────── SD MOSI/CMD
ESP32 GPIO 12 ──────────────────────────────── SD SCLK/CLK
ESP32 GPIO 13 ──────────────────────────────── SD MISO/DAT0
                                                SD VDD ── 3V3 (via C_dec)
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| J3 | microSD socket | Push-push or hinged, standard pinout | With card-detect switch if available |
| R21 | CS pull-up | 10kΩ (0402) | Keeps SD deselected during boot |
| C9 | SD VDD decoupling | 100nF / 10V (0402) | At socket VDD pin |
| U4 | ESD protection (optional) | TPD4E05U06 (USON-6) | On all 4 SPI lines |

### Pin Connections

| SD Function | ESP32-S3 GPIO |
|------------|:------------:|
| CS | GPIO 10 |
| MOSI / CMD | GPIO 11 |
| SCLK / CLK | GPIO 12 |
| MISO / DAT0 | GPIO 13 |
| VDD | 3V3 |
| GND | GND |

### Design Notes

- SPI mode (not SDIO) — 4 signal lines
- 10kΩ pull-up on CS ensures SD card is deselected during ESP32 boot (avoids SPI bus contention)
- TPD4E05U06 ESD protection optional — provides ±8kV contact protection on all SPI lines
- SD card is optional feature — provide footprint even if not populated in v1.0

---

## 8. USB-C + CP2102N

### Schematic

```
USB-C Connector
  VBUS ── TPS2051B ── USB_5V
  D+ ──── USBLC6 ──── CP2102N D+
  D− ──── USBLC6 ──── CP2102N D−
  CC1 ── 5.1kΩ ── GND
  CC2 ── 5.1kΩ ── GND
  SHIELD ── GND (via 1MΩ + 100pF)

CP2102N
  TXD ── ESP32 GPIO 44 (UART0 RX)
  RXD ── ESP32 GPIO 43 (UART0 TX)
  DTR ──┐
  RTS ──┤── Auto-program circuit ── ESP32 EN, IO0
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| J4 | USB-C receptacle | USB-C 2.0 UFP (16-pin mid-mount) | Device mode only |
| U5 | USB-UART bridge | CP2102N-A02-GQFN24 | 48MHz internal osc |
| U6 | Load switch | TPS2051BDBVR (SOT-23-5) | 500mA current limit |
| U7 | ESD protection | USBLC6-2SC6 (SOT-23-6) | On D+/D− |
| R22, R23 | CC pull-downs | 5.1kΩ (0402) each | UFP advertisement |
| R24 | Shield to GND | 1MΩ (0402) | |
| C10 | Shield filter | 100pF / 50V (0402) | |
| C11 | CP2102N VDD | 100nF / 10V (0402) | |
| C12 | CP2102N REGIN | 1µF / 10V (0402) | |
| C13 | CP2102N REGIN | 100nF / 10V (0402) | |

### Auto-Program Circuit

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| Q3 | NPN transistor | MMBT3904 (SOT-23) | DTR-driven |
| Q4 | NPN transistor | MMBT3904 (SOT-23) | RTS-driven |
| R25 | Q3 base resistor | 10kΩ (0402) | |
| R26 | Q4 base resistor | 10kΩ (0402) | |
| R27 | EN pull-up | 10kΩ (0402) | EN to 3V3 |
| C14 | EN filter | 1µF / 10V (0402) | EN to GND |
| R28 | IO0 pull-up | 10kΩ (0402) | IO0 to 3V3 |

### Auto-Program Wiring

```
DTR ── R25 ── Q3(Base)
               Q3(Collector) ── ESP32 IO0 (GPIO 0)
               Q3(Emitter) ── RTS

RTS ── R26 ── Q4(Base)
               Q4(Collector) ── ESP32 EN
               Q4(Emitter) ── DTR
```

### CP2102N Pin Connections

| CP2102N Pin | Connection |
|-------------|------------|
| D+ | USB-C D+ (post-ESD) |
| D− | USB-C D− (post-ESD) |
| TXD | ESP32 GPIO 44 (UART0 RX) |
| RXD | ESP32 GPIO 43 (UART0 TX) |
| DTR | Auto-program Q3 base (via R25) |
| RTS | Auto-program Q4 base (via R26) |
| VDD | 3V3 (via C11) |
| REGIN | USB_5V (via C12, C13) |
| GND | GND |

### Design Notes

- Circuit is identical to controller board (Section 8 of controller wiring doc)
- ESP32-S3 native USB (GPIO 19/20) is **not available** — those GPIOs are used for touch I²C Bus 0
- CP2102N is therefore **required** for USB programming on this board
- TPS2051B current limits USB VBUS; display board draws minimal current from USB (programming only)

---

## 9. ESP32-S3 Support Circuitry

### Power & Decoupling

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| C15, C16 | Bulk decoupling | 2× 10µF / 10V ceramic (0805) | Near VDD3P3 pins |
| C17–C20 | HF decoupling | 4× 100nF / 10V ceramic (0402) | One per VDD pin cluster |

### Strapping & Reset

| Pin | Connection | Notes |
|-----|-----------|-------|
| EN | 10kΩ pull-up to 3V3 + 1µF to GND | Also driven by auto-program circuit (Section 8) |
| IO0 (GPIO 0) | 10kΩ pull-up to 3V3 | Also driven by auto-program circuit (Section 8) |
| GPIO 45 | Used for LCD R3 | **Note:** GPIO 45 is a strapping pin (VDD_SPI). R3 output is low during boot (LCD not initialized), which selects 3.3V VDD_SPI — correct behavior. |
| GPIO 46 | Used for LCD B5 | **Note:** GPIO 46 is a strapping pin (boot mode). B5 output is low during boot — selects SPI boot — correct behavior. |

### Antenna Keepout

- Maintain **≥10mm copper-free zone** from ESP32-S3 module antenna edge
- No traces, vias, copper pours, or components in antenna keepout area
- Ground plane on layer 2 should stop at the keepout boundary

### Crystal

- ESP32-S3-WROOM-1 module includes internal 40MHz crystal — no external crystal needed

### Design Notes

- GPIO 45 and 46 are used for LCD RGB data (R3 and B5). During boot, these pins determine strapping configuration. Since the LCD data outputs are low/floating at reset, the strapping values default correctly (3.3V VDD_SPI, SPI boot mode). No additional pull-down resistors are needed beyond what the LCD outputs provide.

---

## 10. GPIO Assignment Summary

### Complete GPIO Map

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0 | IO0 (Boot strap) | Input | 10kΩ pull-up; auto-program driven |
| 1 | LCD B7 | Output | Blue bit 7 (MSB) |
| 2 | Backlight PWM | Output | 800Hz LEDC, AO3400A gate |
| 3 | LCD B4 | Output | Blue bit 4 |
| 4 | LCD G7 | Output | Green bit 7 (MSB) |
| 5 | LCD G2 | Output | Green bit 2 (LSB) |
| 6 | LCD G3 | Output | Green bit 3 |
| 7 | LCD G4 | Output | Green bit 4 |
| 8 | LCD B3 | Output | Blue bit 3 (LSB) |
| 9 | LCD B6 | Output | Blue bit 6 |
| 10 | SD Card CS | Output | SPI CS (optional, 10kΩ pull-up) |
| 11 | SD Card MOSI | Output | SPI MOSI (optional) |
| 12 | SD Card SCLK | Output | SPI CLK (optional) |
| 13 | SD Card MISO | Input | SPI MISO (optional) |
| 14 | LCD R7 | Output | Red bit 7 (MSB) |
| 15 | LCD G5 | Output | Green bit 5 |
| 16 | LCD G6 | Output | Green bit 6 |
| 17 | Sensor I²C SCL | Output (OD) | I²C Bus 1, 4.7kΩ pull-up |
| 18 | Sensor I²C SDA | Bidir (OD) | I²C Bus 1, 4.7kΩ pull-up |
| 19 | Touch I²C SDA | Bidir (OD) | I²C Bus 0, 4.7kΩ pull-up |
| 20 | Touch I²C SCL | Output (OD) | I²C Bus 0, 4.7kΩ pull-up |
| 21 | LCD R6 | Output | Red bit 6 |
| 22 | Touch INT (optional) | Input | DNP 0Ω link from GT911 INT |
| 38 | Touch RST | Output | GT911 reset, 10kΩ pull-up |
| 39 | LCD HSYNC | Output | Horizontal sync |
| 40 | LCD DE | Output | Data enable |
| 41 | LCD VSYNC | Output | Vertical sync |
| 42 | LCD DCLK | Output | Pixel clock |
| 43 | UART0 TX | Output | To CP2102N RXD |
| 44 | UART0 RX | Input | From CP2102N TXD |
| 45 | LCD R3 | Output | Red bit 3 (LSB) — also strapping pin |
| 46 | LCD B5 | Output | Blue bit 5 — also strapping pin |
| 47 | LCD R5 | Output | Red bit 5 |
| 48 | LCD R4 | Output | Red bit 4 |

### Conflict Check

- **GPIO 17/18**: Sensor I²C on this board. On the controller board these are RS-485 UART1 — **no conflict** (separate boards).
- **GPIO 19/20**: Touch I²C — prevents use of ESP32-S3 native USB, hence CP2102N is required.
- **GPIO 45/46**: Strapping pins reused for LCD. Boot-time state is compatible (both float low → correct strapping).
- **GPIO 43/44**: UART0 for CP2102N — standard.
- **GPIO 0**: Boot strapping — auto-program circuit managed.
- **GPIO 10–13**: SD card SPI — optional; no conflict with LCD GPIOs.
- **GPIO 22**: Reserved for optional touch INT — not used by any other subsystem.
- All 21 LCD RGB GPIOs verified against spec — no overlaps with I²C, UART, or SPI functions.
