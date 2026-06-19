# ESP32-S3 Bench-Validation Notes (learnings)

Hard-won learnings from bringing up the controller firmware on a stand-in
**Adafruit Feather ESP32-S3 (8 MB, no PSRAM)** to validate Plan 2 (panic
breadcrumb) and the Plan 4 board port. Captured so the next person (or the real
Waveshare bring-up) doesn't re-derive them.

## 1. pioarduino S3 build config (platform-espressif32 55.03.37)

- **`network_provisioning/network_config.h: No such file`** — the prebuilt Arduino
  `Network` lib includes this header (gated by `CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI`),
  but it isn't shipped in the prebuilt framework. Fix: trigger the **hybrid source
  build**, which regenerates `sdkconfig` with `network_provisioning` removed (so the
  `#if` is false and the include is skipped).
- **The hybrid build is triggered by `custom_sdkconfig`**, NOT by `custom_component_remove`
  alone. The production controller/thermostat envs set both. A single benign line
  (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`) is enough to switch modes; then
  `custom_component_remove` (incl. `espressif/network_provisioning`) takes effect.
- **It's stateful/flaky:** a plain `pio run` after a partial/failed hybrid attempt can
  fall back to the prebuilt path and re-fail. Reliable sequence: `pio run -e <env> -t clean`
  **then** `pio run -e <env>` from a clean build dir. Remove stray auto-generated
  `sdkconfig.defaults` / `sdkconfig.<env>` from the project root if a build half-completed.
- **Partition CSV is resolved from the PROJECT ROOT in IDF/hybrid mode**, not the
  framework. The repo already vendors `default_16MB.csv` + `partitions_no_spiffs.csv`;
  we added `default_8MB.csv` the same way for the S3 env.
- First hybrid build is slow (~10 min, rebuilds ESP-IDF); then cached.

## 2. Avoiding all of the above for a quick test: the minimal selftest env

For pure logic validation that doesn't need WiFi/MQTT, build **only the file under test
+ its deps** (`build_src_filter = -<*> +<bench/...> +<...>`) with **no `lib_deps`**. With
no networking pulled in, the Arduino `Network` lib never compiles, so the
`network_provisioning` issue never arises and the build is a fast (~13 s) prebuilt build.
See `[env:s3-panic-selftest]`.

## 3. ESP32-S3 USB / serial — the part that actually blocked us

The Feather's only USB goes to the S3's native USB pins, shared by two peripherals
selected at build time (`ARDUINO_USB_MODE`):

| Mode | Device | DTR/RTS behavior | Reading it |
|---|---|---|---|
| `USB_MODE=1` (HWCDC / USB-Serial-JTAG) — devkitc-1 default | `303a:1001` | **Interpreted as EN/GPIO0** (auto-download) | Opening the port pulses reset → chip drops to **download mode**. Hostile to passive reads. |
| `USB_MODE=0` (TinyUSB CDC) — Feather default | `239a:8113` | **Ignored** | Opening the port does NOT reset the chip. Use this for headless reads. |

- **`pio device monitor` can't run headlessly** — it `tcgetattr()`s the console to go
  raw; with piped stdin (no TTY) it dies with `termios.error: Inappropriate ioctl`. Not
  a pio bug — interactive monitors need a real terminal. Use `python -m serial`/`cat`
  for non-interactive capture instead.
- **Resetting the S3 out of download into the app:** `esptool --after hard_reset`
  was unreliable over the JTAG; **`esptool --after watchdog_reset`** is the
  USB-Serial-JTAG-safe one.
- **First flash of a board running app-firmware needs download mode:** hold BOOT, tap
  RESET, release BOOT → enumerates as `303a:1001` (ROM USB-Serial-JTAG), then flashable.
  Avoid *double*-tapping RESET on Adafruit boards (that's the UF2 mass-storage bootloader).

## 4. The wall: KVM USB passthrough

The dev VM is **KVM with USB passthrough** (`systemd-detect-virt=kvm`, QEMU tablet in
`lsusb`). The passthrough **cannot keep up with the S3's re-enumeration on every
reset/boot** — flashing works (device sits still in download mode), but the moment the
chip resets to run and re-enumerates, the passthrough drops the `/dev/ttyACM` node
(observed: device dropping even at idle, and `opens=0` for 36 s straight). No firmware
or tooling change fixes this; it's below the OS.

## 5. The fix: drive a board on a real host (remote bench)

`piserial5.lan` = **Orange Pi (aarch64, Ubuntu Jammy), `virt: none`, native USB**,
reachable on the LAN, SSH as `ryan` via key auth, in `sudo`+`docker` groups (not
`dialout` — `sudo chmod` the port, like locally). Has `python3`+`nc` but **needs
`esptool`+`pyserial`** (`pip install --user esptool pyserial`).

Workflow to validate on the remote bench:
1. Move the board to a piserial5 USB port (native USB → stable across resets).
2. Build firmware **here** (full pio toolchain), `scp` the artifacts
   (`bootloader.bin` @0x0, `partitions.bin` @0x8000, `boot_app0.bin` @0xe000,
   `firmware.bin` @0x10000) to piserial5.
3. SSH in; `sudo chmod a+rw /dev/ttyACM*`; flash with `esptool ... write_flash ...`;
   read serial with a small pyserial/`cat` reader (TinyUSB build → reads don't reset).
4. (Alt) `esptool` also supports `--port rfc2217://piserial5.lan:PORT` if an
   `esp_rfc2217_server` is run on the Pi — flash+reset entirely from here, no scp.

## 6. What this validated — and the BUG it found

- ✅ Controller firmware **and** the minimal selftest **build for ESP32-S3** (real
  de-risk for Plan 4).
- ✅ Flashing + verifying works over the JTAG link.
- ✅ **Remote bench works:** reading the Feather on piserial5's native USB (via the
  stable `/dev/serial/by-id/...` symlink + a reconnecting `cat` loop) captured the
  serial cleanly across reboots — what the KVM passthrough never could.
- 🛑→✅ **BUG FOUND (Plan 2) and ROOT-CAUSED + FIXED.** On-device the selftest
  **panic-looped**: every boot reported `recovered breadcrumb: none`, forced the
  panic, rebooted, repeated. The native unit tests pass (they cover
  `panic_breadcrumb_format`/`_present`, not the on-silicon capture-across-reset),
  which is exactly why this needed on-device validation.
  - **Root cause (found by inspection, no hardware): the firmware was not linked with
    `-Wl,--wrap=esp_panic_handler`.** The Arduino core's `set_arduino_panic_handler()`
    path only works when that wrap flag redirects `esp_panic_handler` to the core's
    `__wrap_esp_panic_handler` (which calls the registered callback). Arduino IDE adds
    the flag via `platform.txt`; **PlatformIO/pioarduino does NOT** (confirmed: the
    real link had `--wrap=log_printf`/`--wrap=longjmp` but not `esp_panic_handler`).
    So `on_panic` was dead code → breadcrumb never written. This affected EVERY
    PlatformIO build, incl. the production controller — the breadcrumb has never worked.
  - **Fix:** add `-Wl,--wrap=esp_panic_handler` via a shared `[panic_wrap]` ini
    section to the controller / controller-s3 / selftest envs (commit on the branch).
    Confirmed the flag is now in the link; builds clean.
  - ✅ **FIX VALIDATED ON HARDWARE (2026-06-16, via piserial5, button-free).** After
    flashing the fixed firmware: boot1 panics, **boot2 recovers `core1 pc=0x42002579
    bt=0x42002576,0x42005e38,0x4037b891`** and idles (no re-loop). addr2line resolves
    those to `loop() at panic_selftest.cpp:51` (the deliberate null write) → `loopTask`
    → `vPortTaskWrapper` — exact crash site + correct backtrace. The breadcrumb works.

### Button-free flashing of the Feather over SSH (the recipe that worked)
piserial5 is offline, so: download the **standalone esptool aarch64 binary** on a
machine with internet (`github.com/espressif/esptool/releases` →
`esptool-vX-linux-aarch64.tar.gz`), `scp` + untar it onto piserial5. Then, all over SSH:
1. **udev rule** so re-enumerated ESP ports are readable without the dialout group:
   `SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666"` (+ `239a`/`8113`);
   `sudo udevadm control --reload-rules && sudo udevadm trigger`.
2. **1200-baud touch → ROM download mode** (no BOOT button!): `stty -F <feather-tty> 1200 hupcl`.
   The Adafruit ESP32-S3 TinyUSB CDC reboots to the **303a USB-Serial-JTAG download** unit
   (NOT UF2) — stable, since download mode doesn't run the looping app. A clean by-id
   appears: `usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00`.
3. **Flash** via that JTAG port: `sudo esptool --chip esp32s3 --port <jtag-byid>
   --after hard_reset write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0xe000
   boot_app0.bin 0x10000 firmware.bin` (offsets from `pio run -t upload -v`).
4. **Read** the booted app via the Feather TinyUSB by-id (`usb-Adafruit_Feather...`)
   with a reconnecting `cat` loop (TinyUSB ignores DTR/RTS → reads don't reset it).

This is a fully remote, button-free flash→read→validate loop on a real host.
  - General lesson: **any Arduino-core feature gated behind a `platform.txt`
    `compiler.*.elf.flags` linker flag is silently absent under PlatformIO.** Check the
    actual link (`pio run -v | grep -- --wrap`) when a core hook "does nothing."
