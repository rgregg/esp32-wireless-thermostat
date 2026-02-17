# PlatformIO Port Plan

## Current Status
- Completed: ESPHome furnace YAML behavior inventory.
- Completed: Controller runtime port (safety logic, sequencing, relay/fan/filter behavior).
- Completed: Thermostat runtime port (command sequencing, telemetry/debounce, connectivity tracking).
- Completed: Bidirectional ESP-NOW transport adapters and node wiring.
- Completed: Display-domain app/model for UI-facing values, unit conversion, status text, and weather icon mapping.
- Completed: Native automated tests for core components.
- Completed: ESP32-S3 thermostat firmware wiring (RGB panel flush, GT911 touch input, backlight dimming, AHT20 polling, runtime loop glue).
- Completed: LVGL thermostat pages/widgets/interactions for Home/Fan/Mode/Settings/Screensaver flows.

## Remaining Work
- Hardware validation on real controller + display boards.
- Optional: add transport ACK/retry metrics and persistent diagnostics.
- Optional: hardware-in-loop verification scripts for both devices.

## Validation
Passing now:
- `pio run -e native`
- `pio run -e native-tests`
- `pio run -e esp32dev-controller`
- `pio run -e esp32s3-display`
- `./.pio/build/native-tests/program`

Native tests currently validate:
- command codec roundtrip + sequence handling
- controller failsafe/lockout and circulation/filter timing
- controller app auto HVAC behavior from indoor temperature
- thermostat app debounce and command publication
- display app/model formatting, conversion, weather mapping, and status text
