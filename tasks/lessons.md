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
