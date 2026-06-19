# Lessons Learned

## ESP32-S3 RGB LCD Display

### Bounce buffer DMA shift (intermittent wrapping/artifacts)
- **Symptom**: Top lines of display wrap to bottom, visual artifacts appear intermittently
- **Root cause**: PSRAM bandwidth contention between bounce buffer DMA, WiFi, and flash cache
- **Why ESPHome works but Arduino doesn't**: ESPHome compiles ESP-IDF from source with `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` and `CONFIG_SPIRAM_RODATA=y`, which moves flash code/rodata into PSRAM and frees the flash cache bus. The pioarduino pre-built Arduino libraries have these options **disabled** in their sdkconfig.
- **What doesn't fix it**: Changing porch timing values, lowering pixel clock, increasing bounce buffer size, `direct_mode`, semaphore drain, `esp_lcd_rgb_panel_restart()`, `bb_invalidate_cache`. These are all mitigations, not fixes.
- **Real fix** (implemented): Use HybridCompile (`custom_sdkconfig` in platformio.ini) to rebuild ESP-IDF from source. Must also use `custom_component_remove` to strip ~23 unused managed components (esp_insights, esp_rainmaker, zigbee, esp-sr, etc.) that would break the build. Also add `lib_ignore` for unused Arduino-level libs.
- **Constraint**: `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` is already enabled in the pre-built libs but insufficient without the SPIRAM XIP options.

### LCD timing parameters
- The ESP32-8048S043C (Sunton board) works with porch values of 8/8/4 at 14 MHz pixel clock
- Increasing porches to datasheet values (hsync_bp=46, hsync_fp=210) causes the panel to fail to initialize (black screen/color bars)
- Pixel clock below ~12 MHz also causes initialization failure
- The original timing values (8/8/4 all around, 14 MHz) are the only working combination found

### LVGL configuration for panel-owned double buffers
- `full_refresh = 1` AND `direct_mode = 1` should both be set
- `LV_MEM_SIZE` at 48 KB is too small (device hangs during UI init), 128 KB works
- The LVGL heap is internal SRAM — reducing it too aggressively causes silent hangs

## Build system

### pioarduino `-D` flags vs sdkconfig
- `-D` build flags only affect application code compilation, NOT pre-compiled ESP-IDF libraries
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `CONFIG_SPIRAM_RODATA`, `CONFIG_LCD_RGB_RESTART_IN_VSYNC` as `-D` flags have NO effect on framework behavior
- Must use `custom_sdkconfig` (HybridCompile) to actually change these options

### PubSubClient default buffer
- Default `MQTT_MAX_PACKET_SIZE` is 256 bytes — must call `setBufferSize(1024)` for HA discovery payloads

### Firmware version stamp reflects the build-time git state
- `firmware_version` is generated from `git describe --tags --dirty` at build time.
- If you build before committing, the binary is stamped with the *previous* commit + `-dirty`, even though it contains your uncommitted edits. The flashed code is correct but the version label is misleading.
- **Before flashing: commit first, then (re)build, then flash** — so the embedded version matches the deployed commit. Verify with `strings firmware.bin | grep 'v[0-9]'`.

### PlatformIO silently drops `platform.txt` linker flags
- Arduino-IDE-only build flags live in the framework's `platform.txt` (e.g. `compiler.c.elf.flags` has `-Wl,--wrap=esp_panic_handler`). **PlatformIO/pioarduino does NOT read `platform.txt`**, so any Arduino-core feature gated behind such a flag is silently inert under PlatformIO.
- This is how the panic-PC breadcrumb "passed" unit tests but did nothing on hardware: `set_arduino_panic_handler()` needs `-Wl,--wrap=esp_panic_handler` to redirect `esp_panic_handler` → the core's `__wrap_esp_panic_handler` (which calls the callback). Without the flag the callback is never invoked.
- **When an Arduino-core hook "does nothing," check the actual link:** `pio run -e <env> -v 2>&1 | grep -- '--wrap'`. Re-add any missing `platform.txt` link flags in `platformio.ini` `build_flags`.
- Corollary: **on-device validation catches what unit tests can't.** Linker/runtime/RTC behavior is invisible to native tests.

### `PLATFORMIO_BUILD_FLAGS` and `platformio.ini` edits wipe `.pio/build`
- PlatformIO stores `.pio/build/project.checksum` (a hash of config incl. the
  `PLATFORMIO_BUILD_FLAGS` env var). When it changes, pio **deletes the entire
  `.pio/build/`** to force clean rebuilds — wiping *all* envs, not just the one you build.
- Symptom that bit me: built a display env with `PLATFORMIO_BUILD_FLAGS=-D...` to inject
  config, then built another env without it → the display's `firmware.bin` vanished and a
  stale `/tmp/*.bin` got flashed instead (the scp had silently failed; the flash used old
  files). Also turned every "incremental" rebuild into a slow full rebuild.
- **Rules:**
  - Don't toggle `PLATFORMIO_BUILD_FLAGS` between builds. Put per-variant config in a
    committed `[env:...]` (use `extends`); env defs coexist at a stable checksum.
  - To build multiple variants, pass them in ONE invocation: `pio run -e A -e B` — same
    checksum, no inter-env wipe.
  - After any flash-prep `scp`, verify it succeeded and check the remote file's timestamp
    before flashing (`ls -la`); a failed scp leaves stale bins that flash "successfully."
