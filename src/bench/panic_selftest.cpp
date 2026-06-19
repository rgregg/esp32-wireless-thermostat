// Minimal on-device self-test for the panic-PC breadcrumb (Plan 2).
//
// Exercises ONLY the panic-capture path — no WiFi/MQTT/NimBLE — so it builds fast
// and flashes onto any ESP32-S3 bench board. Validates over serial alone:
//   boot 1: breadcrumb == "none"  -> deliberately panic (StoreProhibited)
//   boot 2: breadcrumb == "core<N> pc=0x.. bt=0x..,.."  -> recovered, then idle
//
// Build with the s3-panic-selftest env. Compare the recovered pc/backtrace against
// the chip's own panic dump (printed on serial right before reboot) to confirm the
// breadcrumb captures the real crash address. Resolve with addr2line against
// .pio/build/s3-panic-selftest/firmware.elf.
#if defined(PANIC_SELFTEST)

#include <Arduino.h>
#include <cstring>

#include "controller/panic_breadcrumb_hook.h"

namespace {
bool g_had_prior_panic = false;
uint32_t g_boot_ms = 0;
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2500);  // let the USB-CDC/JTAG console enumerate before we print

  char buf[160];
  thermostat::panic_breadcrumb_recover_on_boot(buf, sizeof(buf));
  g_had_prior_panic = (std::strncmp(buf, "none", 4) != 0);

  Serial.println();
  Serial.println("==== panic-breadcrumb self-test ====");
  Serial.printf("[selftest] recovered breadcrumb: %s\n", buf);
  if (g_had_prior_panic) {
    Serial.println("[selftest] RESULT: prior panic was captured + recovered from RTC. "
                   "Idling (will not re-crash).");
  } else {
    Serial.println("[selftest] no prior panic recorded. Forcing a StoreProhibited "
                   "panic in 5s — note the PC in the chip's panic dump below, then "
                   "compare it to the breadcrumb on the next boot.");
  }
  g_boot_ms = millis();
}

void loop() {
  if (!g_had_prior_panic && (millis() - g_boot_ms) > 5000) {
    Serial.println("[selftest] forcing panic now (write to null pointer)...");
    Serial.flush();
    volatile int *p = reinterpret_cast<int *>(0);
    *p = 0x1234;  // StoreProhibited — panic handler captures the PC into RTC
    Serial.println("[selftest] unreachable");
  }
  delay(100);
}

#endif  // PANIC_SELFTEST
