#pragma once

#include <stddef.h>

// Callers size their output buffer with kPanicBreadcrumbTextMax.
#include "controller/panic_breadcrumb.h"

namespace thermostat {

// Recover the panic breadcrumb left by the previous boot (if any), format it
// into `out` ("none" when absent or on power-on), and invalidate the record so
// it is reported once. Call once early in setup(). Firmware-only.
void panic_breadcrumb_recover_on_boot(char *out, size_t out_len);

}  // namespace thermostat
