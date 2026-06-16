// On-board validation of crash COREDUMP capture on the Waveshare board.
//
// Proves the full coredump-to-flash pipeline end-to-end on real hardware:
//   1. On boot, ask esp_core_dump_image_get() whether a coredump is present in the
//      coredump flash partition (left by the PREVIOUS run's deliberate crash).
//      - present  -> capture works: print its flash address + size, then erase it so
//        the next run starts clean.
//      - absent   -> first run (or capture failed).
//   2. ~8s after boot, deliberately crash (null-pointer write) so the IDF panic
//      handler writes an ELF coredump to the coredump partition.
//   3. The board reboots and step 1 runs again — now it should FIND the coredump.
//
// So a single board, across one auto-reboot, proves: panic -> coredump written ->
// readable on next boot. No host tooling required for the core proof. (Host-side
// `esp-coredump info_corefile` against firmware.elf is a bonus, done separately.)
//
// Requires CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y (Arduino-ESP32 default) and a
// partition table with a `coredump` partition (default_16MB.csv has one).
#if defined(WAVESHARE_COREDUMP_VALIDATE)

#include <Arduino.h>
#include "esp_core_dump.h"

namespace {
volatile uint32_t *kNullPtr = nullptr;  // volatile so the crash store isn't optimized out
}

void setup() {
  // Silence the on-board buzzer (GPIO46) first, as the real firmware does.
  pinMode(46, OUTPUT);
  digitalWrite(46, LOW);

  Serial.begin(115200);
  delay(2500);
  Serial.println();
  Serial.println("==== Waveshare coredump capture validation ====");

  size_t addr = 0, size = 0;
  esp_err_t got = esp_core_dump_image_get(&addr, &size);
  if (got == ESP_OK && size > 0) {
    Serial.printf("[coredump] PRESENT in flash: addr=0x%08X size=%u bytes\n",
                  (unsigned)addr, (unsigned)size);
    Serial.println("[coredump] *** CAPTURE VALIDATED *** (this coredump was written by the");
    Serial.println("[coredump]     previous run's deliberate crash and survived the reboot)");
    esp_err_t er = esp_core_dump_image_erase();
    Serial.printf("[coredump] erased for a clean next run -> %s\n",
                  er == ESP_OK ? "ok" : "ERASE FAILED");
    Serial.println("[coredump] idling — re-flash or reset to run the crash cycle again.");
    while (true) delay(1000);
  }

  Serial.printf("[coredump] none present yet (image_get -> %d). This is the first run.\n",
                (int)got);
  Serial.println("[coredump] Will deliberately CRASH in 8s (null-ptr write) so the panic");
  Serial.println("[coredump] handler writes a coredump to flash. The board will reboot and");
  Serial.println("[coredump] this sketch will then detect the captured coredump.");
}

void loop() {
  static uint32_t boot_ms = 0;
  if (boot_ms == 0) boot_ms = millis();
  const uint32_t elapsed = millis() - boot_ms;

  static uint32_t last = 0;
  if (elapsed - last >= 1000) {
    last = elapsed;
    Serial.printf("[coredump] crashing in %lus...\n", (unsigned long)(8 - elapsed / 1000));
  }

  if (elapsed >= 8000) {
    Serial.println("[coredump] >>> CRASH NOW (StoreProhibited on null pointer) <<<");
    Serial.flush();
    *kNullPtr = 0xDEADBEEF;  // deliberate fault -> IDF panic -> coredump to flash -> reboot
    // not reached
  }
  delay(20);
}

#endif  // WAVESHARE_COREDUMP_VALIDATE
