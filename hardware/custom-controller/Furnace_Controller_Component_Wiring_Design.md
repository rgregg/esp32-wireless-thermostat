# Furnace Controller — Component Wiring Design

**Board:** Furnace Controller Custom PCB v1.1
**MCU:** ESP32-S3-WROOM-1 (N16R8)
**Companion Document:** `Furnace_Controller_Custom_PCB_Hardware_Design_Spec_v1.1.md`

This document specifies the complete wiring for every subsystem: component values, pin connections, and design notes. It serves as the direct input for schematic capture.

---

## 1. 24VAC Input & Rectification

### Schematic

```
Terminal R ──┬── MOV ──┬── PTC ── Bridge(AC1)
             │         │
Terminal C ──┼── MOV ──┼── Bridge(AC2)
             │         │
            GND       GND
                              Bridge(+) ── C_bulk ──┬── DC_RAW+
                              Bridge(−) ────────────┴── DC_RAW− (GND)
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| RV1 | MOV | EPCOS B72210S0321K101 (32Vrms, 10mm disc) | Clamps surges on 24VAC input |
| F1 | Resettable PTC | Littelfuse 1812L200/33MR (2A hold, 1812) | Protects against sustained overcurrent |
| BR1 | Full-bridge rectifier | MB6S (600V, 0.5A, SOIC-4) | GBU4J (600V, 4A) if higher margin desired |
| C1 | Bulk electrolytic | 470µF / 63V | Sized for ripple at ~200mA average draw |

### Pin Connections

- **R terminal** → MOV pin 1, PTC input
- **C terminal** → MOV pin 2, bridge AC2
- **PTC output** → bridge AC1
- **Bridge +** → DC_RAW+ net (to buck input)
- **Bridge −** → GND

### Design Notes

- 24VAC RMS range 18–32V → rectified peak 25–45V (minus bridge Vf ≈ 1.1V)
- MOV clamp voltage ~51V (well above 45V peak, catches only transients)
- PTC at 2A hold provides adequate margin (continuous draw < 500mA)

---

## 2. Wide-Input Buck: Rectified DC → 12V_SYS (LMR16030)

### Schematic

```
DC_RAW+ ── C_in ──┬── VIN ┌──────────┐ SW ── L1 ──┬── 12V_SYS
                   │       │ LMR16030 │             │
                   │  BST ─┤          ├─ PGND       C_out
                   │  C_bst│          │             │
                   │       │    FB ───┤             GND
                   │       └──────────┘
                   │            │
                   GND      Divider
                            RFBT ── 12V_SYS
                            RFBB ── GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U1 | Buck regulator | TI LMR16030 (SOIC-8, 4.3–60V in, 3A) | Handles worst-case 45V rectified peak |
| C2, C3 | Input ceramic | 2× 4.7µF / 100V (X7R, 1210) | Close to VIN pin; supplement with C1 bulk |
| C4 | Bootstrap | 0.1µF / 16V ceramic (0402) | Between BST and SW pins |
| L1 | Inductor | 10µH, ≥3A saturation (Würth 744774110) | Shielded, low DCR |
| C5, C6 | Output ceramic | 2× 47µF / 25V (X5R, 1210) | Low ESR for ripple |
| C7 | Output electrolytic | 100µF / 25V | Additional bulk for load transients |
| R1 | Feedback top (RFBT) | 150kΩ (0402, 1%) | |
| R2 | Feedback bottom (RFBB) | 10kΩ (0402, 1%) | |
| R3 | RT (frequency set) | Per datasheet for 500kHz | Consult LMR16030 RT table |

### Pin Connections

| LMR16030 Pin | Connection |
|-------------|------------|
| VIN | DC_RAW+ (via input caps) |
| BST | C4 to SW |
| SW | L1 → output caps → 12V_SYS |
| FB | R1/R2 divider junction |
| PGND | GND (short, wide trace) |
| EN | Float (internal pull-up, always on) |
| RT/CLK | R3 to GND |
| SS | Not used (internal soft-start) |

### Design Notes

- Vout = VFB × (1 + RFBT/RFBB) = 0.75 × (1 + 150k/10k) = **12.0V**
- VFB = 0.75V per LMR16030 datasheet
- EN left floating — internal pull-up enables regulator at power-on
- Layout: input caps and bootstrap cap within 5mm of IC pins; GND via array under thermal pad

---

## 3. USB 5V → 12V Boost (TPS61023)

### Purpose

Enables full relay testing and firmware development from USB power alone, without 24VAC connected.

### Schematic

```
USB_5V_SW ── C_in ──┬── VIN ┌──────────┐ SW ── L2 ──┬── 12V_USB
                     │       │ TPS61023 │             │
                     │       │          │ OUT ────────┤
                     │       │    FB ───┤             C_out
                     │       │   EN  ───┤             │
                     │       └──────────┘             GND
                     GND          │
                              Divider
                              RFBT ── 12V_USB
                              RFBB ── GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U2 | Boost regulator | TI TPS61023 (SOT-23-6) | 0.5A output, 93% efficiency typical |
| C8 | Input ceramic | 4.7µF / 10V (X5R, 0402) | Close to VIN |
| L2 | Inductor | 4.7µH, ≥1A saturation | Shielded, small footprint |
| C9 | Output ceramic | 22µF / 25V (X5R, 1210) | Low ESR |
| R4 | Feedback top | Per TPS61023 datasheet for 12V | |
| R5 | Feedback bottom | Per TPS61023 datasheet for 12V | |

### Pin Connections

| TPS61023 Pin | Connection |
|-------------|------------|
| VIN | USB_5V_SW (post load switch) |
| SW | L2 (to output) |
| OUT | 12V_USB net (to LTC4412 OR input) |
| FB | R4/R5 divider junction |
| EN | Tie to VIN (always on when USB present) |
| GND | GND |

### Design Notes

- Current budget at 12V: ESP32 (~60mA) + 4 relays (4×33mA = 132mA) ≈ 200mA — well within 500mA capability
- Input power from USB: 200mA × 12V / 0.93 ≈ 2.6W → ~520mA from 5V (within USB 500mA limit)
- Only active when USB VBUS present (EN tied to VIN through load switch)

---

## 4. Ideal Diode OR (2× LTC4412)

### Schematic (per channel)

```
12V_source ── SENSE ┌──────────┐ GATE ── Gate(Q_pfet) ── 12V_SYS
                     │ LTC4412  │
              VIN ───┤          ├─ CTL (open-drain status)
                     │    GND ──┤
                     └──────────┘
                                         Source(Q_pfet) ── 12V_source
                                         Drain(Q_pfet) ── 12V_SYS
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U3 | Diode OR controller (AC path) | LTC4412 (SOT-23-6) | Controls P-FET for 24VAC-derived 12V |
| U4 | Diode OR controller (USB path) | LTC4412 (SOT-23-6) | Controls P-FET for USB-derived 12V |
| Q1 | P-FET (AC path) | AO4407A (SO-8, −12A, 30V, RDS(on) 12mΩ) | Handles up to 1.5A from AC supply |
| Q2 | P-FET (USB path) | Si2301 (SOT-23, −2.3A, 20V, RDS(on) ~100mΩ) | Adequate for <500mA USB path |

### Pin Connections (each LTC4412)

| LTC4412 Pin | Connection |
|-------------|------------|
| VIN | Source rail (12V from buck or boost) |
| SENSE | Source side of P-FET (Source pin) |
| GATE | P-FET Gate |
| GND | GND |
| CTL | Optional: pull-up 100kΩ to 3.3V for status monitoring (active low = source active) |
| STAT | Not connected (or optional LED indicator) |

### Design Notes

- LTC4412 drives external P-FET gate to create near-zero-loss OR-ing
- When one source is higher, its P-FET conducts; the other is reverse-biased
- No backfeed from 12V_SYS to USB VBUS
- Combined output: **12V_SYS** bus feeds all downstream circuits

---

## 5. 3.3V Buck (AP63203)

### Schematic

```
12V_SYS ── C_in ──┬── VIN ┌──────────┐ SW ── L3 ──┬── 3V3
                   │       │ AP63203  │             │
                   │  BST ─┤ (3.3V)  ├─ GND        C_out
                   │  C_bst│          │             │
                   │       └──────────┘             GND
                   GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U5 | 3.3V buck | Diodes Inc AP63203WU-7 (SOT-23-6, fixed 3.3V) | 2A, internal compensation |
| C10 | Input ceramic | 10µF / 25V (X5R, 0805) | Close to VIN pin |
| C11 | Bootstrap | 0.1µF / 16V ceramic (0402) | Between BST and SW |
| L3 | Inductor | 3.3µH, ≥2A saturation (Würth 744774033) | Shielded |
| C12, C13 | Output ceramic | 2× 22µF / 10V (X5R, 0805) | Low ESR for stable output |

### Pin Connections

| AP63203 Pin | Connection |
|-------------|------------|
| VIN | 12V_SYS (via C10) |
| BST | C11 to SW |
| SW | L3 → output caps → 3V3 net |
| FB | Internal (fixed 3.3V — no external divider) |
| EN | Float (internal pull-up, always on) |
| GND | GND |

### Design Notes

- Fixed 3.3V output — no feedback resistors needed
- AP63203 includes internal compensation — no external Type III network
- Place input/output caps within 5mm of IC; ground via array under exposed pad

---

## 6. 12V Output Connector (Display Power)

### Schematic

```
12V_SYS ── PTC ── TVS ──┬── Schottky(A→K) ──┬── +12V_OUT (pin 1)
                         │                    │
                         GND                  GND ── GND_OUT (pin 2)
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| F2 | Resettable PTC | 1812L150/12MR (1.5A hold) | Protects cable/connector |
| D1 | TVS | SMAJ16A (16V standoff, 25.6V clamp, SMA) | Absorbs cable transients |
| D2 | Reverse-blocking Schottky | SS14 (1A, 40V, SMA) | Prevents backfeed from display |
| J1 | Connector | JST-VH 2-pin or MicroFit 3.0 2-pin | Locking, polarized |

### Pin Connections

| Connector Pin | Net |
|--------------|-----|
| Pin 1 | +12V_OUT (post PTC, Schottky) |
| Pin 2 | GND |

### Design Notes

- 1.5A PTC covers display worst-case draw (~600mA LCD + backlight + ESP32)
- SMAJ16A clamp at 25.6V protects against cable discharge events
- SS14 Schottky blocks reverse current if display power rail is externally supplied
- Use locking connector to prevent accidental disconnection

---

## 7. Relay Drivers (×4)

### Schematic (per channel)

```
                            +12V_SYS
                               │
                          ┌────┤
                          │  Relay Coil (360Ω)
                          │    │
                    D_fly ─┤    │
                  (cathode)│    │(anode)
                          │    │
                     LED ──┤    │
                     R_led │    │
                          │    │
                          └────┤── Drain(Q)
                               │
GPIO ── R_ser ── Gate(Q) ──┤  N-FET
                           │
                    R_pd ──┤
                           │
                          GND ── Source(Q)
```

### Components (per channel, ×4)

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| K1–K4 | Relay | Omron G5LE-1-DC12 (12V coil, 360Ω, 10A contacts) | Or Hongfa HF46F-G/12-HS1T |
| Q3–Q6 | N-FET | AO3400A (SOT-23, 30V, Vgs(th) ~1.4V, RDS(on) <50mΩ) | Logic-level gate |
| D3–D6 | Flyback diode | 1N4148W (SOD-323) | Cathode to 12V_SYS, anode to FET drain |
| LED1–LED4 | Status LED | Red, 2mA typ (0603) | Visible indication of relay energized |
| R6–R9 | LED resistor | 1kΩ (0402) | (12V − 2V) / 1kΩ ≈ 10mA |
| R10–R13 | Gate series | 100Ω (0402) | Limits gate ringing |
| R14–R17 | Gate pull-down | 10kΩ (0402) | Ensures relay OFF when GPIO floating |

### Pin Connections

| GPIO | Relay | Function |
|------|-------|----------|
| GPIO 4 | K1 | Heat (W) |
| GPIO 5 | K2 | Cool (Y) |
| GPIO 6 | K3 | Fan (G) |
| GPIO 7 | K4 | Spare / AUX |

### Design Notes

- Coil current: 12V / 360Ω = 33mA per relay; total worst-case 4×33mA = 132mA
- LED in parallel path from 12V_SYS through LED + R_led to FET drain — lights when FET on
- 10kΩ pull-down ensures relay stays off during ESP32 boot (GPIOs are Hi-Z at reset)
- 100Ω series gate resistor damps oscillation from gate capacitance + trace inductance
- Flyback diode 1N4148W: 75V reverse, 150mA average — adequate for relay coil energy

---

## 8. USB-C + CP2102N

### Schematic

```
USB-C Connector
  VBUS ── TPS2051B ── USB_5V_SW
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
| J2 | USB-C receptacle | USB-C 2.0 UFP (16-pin mid-mount) | Device mode only |
| U6 | USB-UART bridge | CP2102N-A02-GQFN24 | 48MHz internal osc, no external crystal |
| U7 | Load switch | TPS2051BDBVR (SOT-23-5) | 500mA current limit, active-high EN |
| U8 | ESD protection | USBLC6-2SC6 (SOT-23-6) | On D+/D− lines |
| R18, R19 | CC pull-downs | 5.1kΩ (0402) each | Advertise as UFP (sink) |
| R20 | Shield to GND | 1MΩ (0402) | |
| C14 | Shield filter | 100pF / 50V (0402) | |
| C15 | CP2102N VDD decoupling | 100nF / 10V (0402) | |
| C16 | CP2102N REGIN | 1µF / 10V (0402) | |
| C17 | CP2102N REGIN | 100nF / 10V (0402) | |

### Auto-Program Circuit

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| Q7 | NPN transistor | MMBT3904 (SOT-23) | DTR-driven |
| Q8 | NPN transistor | MMBT3904 (SOT-23) | RTS-driven |
| R21 | Q7 base resistor | 10kΩ (0402) | DTR → Q7 base |
| R22 | Q8 base resistor | 10kΩ (0402) | RTS → Q8 base |
| R23 | EN pull-up | 10kΩ (0402) | EN to 3V3 |
| C18 | EN filter | 1µF / 10V (0402) | EN to GND (RC with R23, τ=10ms) |
| R24 | IO0 pull-up | 10kΩ (0402) | IO0 to 3V3 |

### Auto-Program Wiring

```
DTR ── R21 ── Q7(Base)
               Q7(Collector) ── ESP32 IO0 (GPIO 0)
               Q7(Emitter) ── RTS

RTS ── R22 ── Q8(Base)
               Q8(Collector) ── ESP32 EN
               Q8(Emitter) ── DTR
```

### CP2102N Pin Connections

| CP2102N Pin | Connection |
|-------------|------------|
| D+ | USB-C D+ (post-ESD) |
| D− | USB-C D− (post-ESD) |
| TXD | ESP32 GPIO 44 (UART0 RX) |
| RXD | ESP32 GPIO 43 (UART0 TX) |
| DTR | Auto-program Q7 base (via R21) |
| RTS | Auto-program Q8 base (via R22) |
| VDD | 3V3 (via C15) |
| REGIN | VBUS (via C16, C17) |
| GND | GND |

### TPS2051B Pin Connections

| TPS2051B Pin | Connection |
|-------------|------------|
| IN | USB-C VBUS |
| OUT | USB_5V_SW (to boost converter + CP2102N REGIN) |
| EN | 3V3 (active high — always on) |
| OC | Optional: to GPIO for overcurrent detection (or NC) |
| GND | GND |

### Design Notes

- CP2102N uses internal 48MHz oscillator — no external crystal required
- TPS2051B provides ~500mA current limiting on USB VBUS
- USBLC6-2SC6 provides ±30kV HBM ESD protection on data lines
- Auto-program circuit is the standard ESP32 DTR/RTS cross-coupled NPN topology
- EN RC time constant (10kΩ × 1µF = 10ms) provides stable reset behavior

---

## 9. RS-485 (MAX3485)

### Schematic

```
ESP32 GPIO 17 (TX) ── DI  ┌──────────┐  A ── R_bias_A ── 3V3
ESP32 GPIO 18 (RX) ── RO  │ MAX3485  │      │
ESP32 GPIO 8 (DIR) ── DE  │          │  ┌── J3 pin A
                      RE ──┤          │  │
                     (tied)│   VCC ───┤  │   R_term (120Ω, solder jumper)
                           │   GND ───┤  │
                           └──────────┘  B ── R_bias_B ── GND
                                             │
                                         ┌── J3 pin B
                                         │
                                         J3 pin GND ── GND
```

### Components

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| U9 | RS-485 transceiver | MAX3485 (SOIC-8, VCC 3.0–3.6V) | True 3.3V, half-duplex, ±15kV ESD |
| C19 | VCC decoupling | 100nF / 10V ceramic (0402) | Close to VCC pin |
| R25 | DE/RE pull-down | 10kΩ (0402) | Default receive mode (GPIO 8 low) |
| R26 | Fail-safe bias A | 560Ω (0402) | A to 3V3 (default populated) |
| R27 | Fail-safe bias B | 560Ω (0402) | B to GND (default populated) |
| R28 | Termination | 120Ω (0402) | A to B, solder jumper (default OPEN) |
| J3 | Bus header | 3-pin 2.54mm (A, B, GND) | |

### Pin Connections

| MAX3485 Pin | Connection |
|-------------|------------|
| VCC | 3V3 (via C19) |
| DI | ESP32 GPIO 17 (UART1 TX) |
| RO | ESP32 GPIO 18 (UART1 RX) |
| DE | ESP32 GPIO 8 (direction control) |
| RE̅ | Tied to DE (GPIO 8) |
| A | Bus A (via bias R26, termination R28) |
| B | Bus B (via bias R27, termination R28) |
| GND | GND |

### Design Notes

- **MAX3485 replaces SN65HVD3082E / THVD1500** — both are 5V parts (VCC 4.5–5.5V), not compatible with 3.3V direct supply. MAX3485 is true 3.3V (VCC 3.0–3.6V), pin-compatible SOIC-8.
- DE and RE̅ tied together: GPIO 8 HIGH = transmit, LOW = receive
- 10kΩ pull-down on GPIO 8 ensures default receive mode during boot
- Fail-safe bias ensures defined bus state when no driver active (A pulled high, B pulled low → logic 1 / idle)
- 120Ω termination footprint allows end-of-line termination; default open for short bus runs
- UART1: 9600–115200 baud typical for Modbus RTU

---

## 10. Call-Line Sense Inputs (×5)

### Schematic (per channel)

```
24VAC Call Line ── R_hi (1MΩ) ──┬── BAT54S(A) ──┬── R_filt (100kΩ) ── R_ser (1kΩ) ── ESP32 ADC
                                 │               │
                          R_lo (100kΩ)      C_filt (1µF)
                                 │               │
                                GND          D_clamp (BZX84C3V3)
                                                 │
                                                GND
```

### Components (per channel, ×5)

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| R29–R33 | Divider high | 1MΩ (0402, 1%) | High impedance — won't load thermostat wiring |
| R34–R38 | Divider low | 100kΩ (0402, 1%) | Divide ratio ≈ 11:1 |
| D7–D11 | Half-wave rectifier | BAT54S (SOT-23, dual Schottky) | Low Vf (~0.3V), use one diode of pair |
| R39–R43 | Filter resistor | 100kΩ (0402) | With C = 100ms time constant |
| C20–C24 | Filter cap | 1µF / 10V (0402) | RC filter removes AC ripple |
| D12–D16 | Clamp Zener | BZX84C3V3 (SOT-23, 3.3V) | Clamps to 3.3V for ADC safety |
| R44–R48 | Series protection | 1kΩ (0402) | Current limiting before ADC pin |

### Pin Connections

| Sense Channel | Call Line | GPIO | ADC Channel |
|--------------|-----------|------|-------------|
| W (Heat) | W wire | GPIO 1 | ADC1_CH0 |
| Y (Cool) | Y wire | GPIO 2 | ADC1_CH1 |
| G (Fan) | G wire | GPIO 10 | ADC1_CH9 |
| AUX | AUX wire | GPIO 11 | ADC1_CH10 |
| R (24V) | R wire | GPIO 12 | ADC1_CH11 |

### Design Notes

- Divider: 24VAC × √2 ≈ 34V peak → 34V × 100k/(1M+100k) ≈ 3.1V peak (below 3.3V clamp)
- BAT54S half-wave rectifies → only positive half-cycles reach filter
- 100kΩ + 1µF = 100ms time constant — smooths to near-DC for ADC reading
- BZX84C3V3 Zener clamps any transient above 3.3V
- 1kΩ series resistor limits fault current into ESP32 ADC pin
- High impedance (>1MΩ) — will not affect thermostat call-line signaling
- These are **sense-only** inputs for diagnostics (no firmware support yet)

---

## 11. ESP32-S3 Support Circuitry

### Power & Decoupling

| Ref | Component | Value / Part | Notes |
|-----|-----------|-------------|-------|
| C25, C26 | Bulk decoupling | 2× 10µF / 10V ceramic (0805) | Near VDD3P3 pins |
| C27–C30 | HF decoupling | 4× 100nF / 10V ceramic (0402) | One per VDD pin cluster |

### Strapping & Reset

| Pin | Connection | Notes |
|-----|-----------|-------|
| EN | 10kΩ pull-up to 3V3 + 1µF to GND | Also driven by auto-program circuit (Section 8) |
| IO0 (GPIO 0) | 10kΩ pull-up to 3V3 | Also driven by auto-program circuit (Section 8) |
| GPIO 45 | 10kΩ pull-down to GND | Strapping: VDD_SPI voltage = 3.3V |
| GPIO 46 | 10kΩ pull-down to GND | Strapping: default SPI boot mode |

### Antenna Keepout

- Maintain **≥10mm copper-free zone** from ESP32-S3 module antenna edge
- No traces, vias, copper pours, or components in antenna keepout area
- Ground plane on layer 2 should stop at the keepout boundary

### Crystal

- ESP32-S3-WROOM-1 module includes internal 40MHz crystal — no external crystal needed

---

## 12. GPIO Assignment Summary

### Complete GPIO Map

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| 0 | IO0 (Boot strap) | Input | 10kΩ pull-up; auto-program driven |
| 1 | Call-line sense W | ADC Input | ADC1_CH0 |
| 2 | Call-line sense Y | ADC Input | ADC1_CH1 |
| 3 | — | Reserved | Available |
| 4 | Relay 1 (Heat) | Output | Low-side FET driver |
| 5 | Relay 2 (Cool) | Output | Low-side FET driver |
| 6 | Relay 3 (Fan) | Output | Low-side FET driver |
| 7 | Relay 4 (Spare) | Output | Low-side FET driver |
| 8 | RS-485 DE/RE̅ | Output | Direction control, 10kΩ pull-down |
| 9 | — | Reserved | Available |
| 10 | Call-line sense G | ADC Input | ADC1_CH9 |
| 11 | Call-line sense AUX | ADC Input | ADC1_CH10 |
| 12 | Call-line sense R | ADC Input | ADC1_CH11 |
| 13 | — | Reserved | Available |
| 14 | — | Reserved | Available |
| 15 | — | Reserved | Available |
| 16 | — | Reserved | Available |
| 17 | RS-485 DI (UART1 TX) | Output | To MAX3485 DI pin |
| 18 | RS-485 RO (UART1 RX) | Input | From MAX3485 RO pin |
| 19 | — | Reserved | Available |
| 20 | — | Reserved | Available |
| 21 | — | Reserved | Available |
| 35 | — | Reserved | Input only |
| 36 | — | Reserved | Input only |
| 37 | — | Reserved | Input only |
| 38 | — | Reserved | Available |
| 39 | — | Reserved | Available |
| 40 | — | Reserved | Available |
| 41 | — | Reserved | Available |
| 42 | — | Reserved | Available |
| 43 | UART0 TX | Output | To CP2102N RXD |
| 44 | UART0 RX | Input | From CP2102N TXD |
| 45 | Strapping (VDD_SPI) | — | 10kΩ pull-down; do not use as GPIO |
| 46 | Strapping (Boot mode) | — | 10kΩ pull-down; do not use as GPIO |
| 47 | — | Reserved | Available |
| 48 | — | Reserved | Available |

### Conflict Check

- **GPIO 17/18**: Used for RS-485 UART1 on this board. On the display board these same GPIOs are sensor I²C — **no conflict** since they are separate boards.
- **GPIO 43/44**: UART0 dedicated to CP2102N — matches ESP32-S3 default UART0 pins.
- **GPIO 45/46**: Strapping pins — reserved, not available for general use.
- **GPIO 0**: Boot strapping — managed by auto-program circuit; not available for general use.
- All relay GPIOs (4–7) have no alternate function conflicts on ESP32-S3.
- All ADC sense GPIOs (1, 2, 10, 11, 12) are on ADC1 — ADC2 is not available during Wi-Fi operation.
