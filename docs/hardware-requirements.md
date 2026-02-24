# Hardware Requirements

This project runs as two separate ESP32 firmwares:
- `esp32-furnace-controller` (furnace-side controller)
- `esp32-furnace-thermostat` (touch display thermostat)

This document captures the hardware baseline required by the current codebase.

## Controller Unit (`esp32-furnace-controller`)

### Board/MCU
- ESP32 (classic) board profile compatible with PlatformIO `board = esp32dev`.
- 2.4 GHz Wi-Fi required (also used by ESP-NOW).
- Reference tested unit: ESP32 relay controller board (Amazon ASIN `B0DNYYXQ3X`).

### Power
- Reference tested furnace-side supply: AC-to-DC converter module (Amazon ASIN `B08Q81Y8NM`).
- Size/output the converter for the relay board + ESP32 load with startup margin.

### Required GPIO Outputs
The controller relay driver uses these default output pins:

| Function | GPIO |
|---|---|
| Heat relay | 32 |
| Cool relay | 33 |
| Fan relay | 25 |
| Spare relay | 26 |

Notes:
- Outputs default to active-high (`inverted = false` in `ControllerRelayIoConfig`).
- Relay outputs are software interlocked (heat: 500 ms, others: 1000 ms).
- Use an external relay/interface stage suitable for your HVAC control voltage and isolation needs.

## Thermostat Display Unit (`esp32-furnace-thermostat`)

### Board/MCU
- ESP32-S3 board profile compatible with PlatformIO `board = esp32-s3-devkitc-1`.
- 16 MB flash (matches `board_upload.flash_size = 16MB` build config).
- PSRAM required (framebuffers and LVGL draw buffers allocate in SPIRAM when available).
- Reference tested unit: DIYmalls `ESP32-8048S043C-I` 4.3" capacitive display module (Amazon ASIN `B0CLGCMWQ7`).

### Display Panel
- 800x480 RGB panel (16-bit parallel RGB interface).
- Current timing/pin map is implemented for the `ESP32-8048S043C-I` wiring used by this project.

RGB control pins:

| Signal | GPIO |
|---|---|
| DE | 40 |
| PCLK | 42 |
| VSYNC | 41 |
| HSYNC | 39 |

RGB data pins (`D0..D15`):
`45, 48, 47, 21, 14, 5, 6, 7, 15, 16, 4, 8, 3, 46, 9, 1`

### Touch Controller
- GT911 capacitive touch controller (`I2C addr 0x5D`)
- I2C bus pins: `SDA=19`, `SCL=20` (400 kHz)

### Indoor Sensor
- AHT10 temperature/humidity sensor (Amazon ASIN `B092495GZJ`)
- I2C bus pins: `SDA=18`, `SCL=17` (100 kHz)
- Firmware uses the Adafruit AHTX0 driver layer, so AHT10/AHT20-class devices are supported.

### Backlight
- PWM backlight control on `GPIO2` (LEDC channel 0, 8-bit)

## Shared Requirements (Both Units)
- 2.4 GHz Wi-Fi coverage at install location.
- ESP-NOW enabled on both devices (default channel `6` on both).
- If using encrypted ESP-NOW, configure unicast peer MACs and a shared 16-byte LMK on both units.

## Enclosures (3D Print Files)
- Controller enclosure CAD: [`enclosures/controller_v20.f3d`](../enclosures/controller_v20.f3d)
- Thermostat display enclosure CAD: [`enclosures/display_v27.f3d`](../enclosures/display_v27.f3d)
- For fire compliance, print with a UL-listed filament such as Prusament PETG V0 (or equivalent UL-listed V0-rated material).

## Out of Scope in Current Firmware
The firmware currently does not prescribe:
- exact relay module part number,
- enclosure/thermal/mechanical constraints.

Those should be defined per installation and local electrical code requirements.
