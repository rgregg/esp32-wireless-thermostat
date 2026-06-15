#include "controller/panic_breadcrumb_hook.h"

#if defined(ARDUINO)
#include "controller/panic_breadcrumb.h"
#include "esp_attr.h"
#include "esp32-hal.h"  // set_arduino_panic_handler, arduino_panic_info_t

namespace {

// RTC_NOINIT survives reset/panic — must outlive all C++ static destruction.
// __attribute__((used)) prevents linker GC before panic_breadcrumb_recover_on_boot
// is wired up in setup().
__attribute__((used)) RTC_NOINIT_ATTR thermostat::PanicBreadcrumb g_panic_breadcrumb;

// Called by Arduino's panic wrapper in panic context.
// No heap, no logging, no blocking — stamp and return.
void on_panic(arduino_panic_info_t *info, void * /*arg*/) {
  if (info == nullptr) return;

  g_panic_breadcrumb.magic = thermostat::kPanicBreadcrumbMagic;
  g_panic_breadcrumb.core = static_cast<uint32_t>(info->core);
  g_panic_breadcrumb.pc = reinterpret_cast<uint32_t>(info->pc);

  size_t depth = info->backtrace_len;
  if (depth > thermostat::kPanicBacktraceDepth) depth = thermostat::kPanicBacktraceDepth;
  g_panic_breadcrumb.depth = static_cast<uint32_t>(depth);
  for (size_t i = 0; i < depth; ++i) {
    g_panic_breadcrumb.backtrace[i] = static_cast<uint32_t>(info->backtrace[i]);
  }
}

// Why set_arduino_panic_handler() instead of -Wl,--wrap=esp_panic_handler:
// Arduino-ESP32 already defines __wrap_esp_panic_handler internally and
// exposes set_arduino_panic_handler() as the official single-callback
// extension point.  Adding our own --wrap would cause a multiple-definition
// link error.  Caveat: set_arduino_panic_handler keeps only ONE callback, so
// a later registration by any library would silently replace ours.  That is
// acceptable here — no other panic-hooking components exist in this project.
//
// Register at static-init time so panics during early setup() are captured
// even before panic_breadcrumb_recover_on_boot() is called.
__attribute__((constructor)) void register_panic_hook() {
  set_arduino_panic_handler(on_panic, nullptr);
}

}  // namespace

namespace thermostat {

void panic_breadcrumb_recover_on_boot(char *out, size_t out_len) {
  // Handler is already registered by the constructor above; nothing to do here
  // for registration.  Recover the breadcrumb from the previous boot and
  // invalidate it so it is reported exactly once.
  panic_breadcrumb_format(g_panic_breadcrumb, out, out_len);
  g_panic_breadcrumb.magic = 0;
  g_panic_breadcrumb.pc = 0;
}

}  // namespace thermostat

#else  // !ARDUINO

namespace thermostat {
void panic_breadcrumb_recover_on_boot(char *out, size_t out_len) {
  if (out != nullptr && out_len > 0) out[0] = '\0';
}
}  // namespace thermostat

#endif  // ARDUINO
