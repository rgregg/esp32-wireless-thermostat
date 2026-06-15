# Panic-PC Breadcrumb Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Capture the faulting PC + a short backtrace into RTC-memory when the controller panics, and publish it over MQTT on the next boot — giving crash *localization* on the current 4 MB board (and the future S3 board) without a coredump partition.

**Architecture:** A platform-agnostic `PanicBreadcrumb` record (POD, fixed-size, fully unit-tested for validation + formatting). A firmware-only hook wraps ESP-IDF's `esp_panic_handler` via the linker (`-Wl,--wrap=esp_panic_handler`) to stamp the record into `RTC_NOINIT` memory before the panic completes. On boot, `main` recovers the record, formats it to a string, publishes it as a retained `state/panic_pc` topic + HA diagnostic sensor (mirroring the existing `wdt_section` breadcrumb), then invalidates it (one-shot).

**Tech Stack:** C++ (Arduino-ESP32 / native), ESP-IDF panic API, PlatformIO `native-tests` env, project `TEST_CASE`/`ASSERT_*` harness.

**Validation boundary:** the record module (Tasks 1) is fully native-tested. The panic hook + wiring (Tasks 2–3) compile in the firmware build but their runtime behavior can only be confirmed by forcing a panic on a real device (the current controller). That on-device confirmation is deferred to the user; it does NOT require the new S3 board.

---

## File Structure

- **Create** `include/controller/panic_breadcrumb.h` — `PanicBreadcrumb` POD + `panic_breadcrumb_present()` + `panic_breadcrumb_format()` (all inline, platform-agnostic).
- **Create** `src/tests/test_panic_breadcrumb.cpp` — native unit tests for present/format/clamping.
- **Create** `src/controller/panic_breadcrumb_hook.cpp` — firmware-only (`#if defined(ARDUINO)`): the `RTC_NOINIT` record instance, `__wrap_esp_panic_handler`, `panic_breadcrumb_capture()`, and `panic_breadcrumb_recover_on_boot()`.
- **Create** `include/controller/panic_breadcrumb_hook.h` — declares `panic_breadcrumb_recover_on_boot(char *out, size_t out_len)` for `main`.
- **Modify** `platformio.ini` — add `-Wl,--wrap=esp_panic_handler` to the `esp32-furnace-controller` env `build_flags`.
- **Modify** `src/esp32_controller_main.cpp` — recover on boot into a `g_ctrl_panic_pc` String; publish `state/panic_pc`; add the HA discovery sensor; publish in the runtime-state path (next to `wdt_section`).

---

## Task 1: `PanicBreadcrumb` record module (TDD, native)

**Files:**
- Create: `include/controller/panic_breadcrumb.h`
- Create: `src/tests/test_panic_breadcrumb.cpp`

- [ ] **Step 1: Write the failing tests**

Create `src/tests/test_panic_breadcrumb.cpp`:

```cpp
#if defined(THERMOSTAT_RUN_TESTS)
#include <cstring>

#include "controller/panic_breadcrumb.h"
#include "test_harness.h"

using thermostat::PanicBreadcrumb;

TEST_CASE(panic_breadcrumb_absent_formats_none) {
  PanicBreadcrumb b{};  // all zero: magic invalid
  ASSERT_TRUE(!thermostat::panic_breadcrumb_present(b));
  char out[64];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "none");
}

TEST_CASE(panic_breadcrumb_present_requires_magic_and_pc) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0;  // no pc -> not present
  ASSERT_TRUE(!thermostat::panic_breadcrumb_present(b));
  b.pc = 0x400d1234;
  ASSERT_TRUE(thermostat::panic_breadcrumb_present(b));
}

TEST_CASE(panic_breadcrumb_formats_pc_core_and_backtrace) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.core = 1;
  b.pc = 0x400d1234;
  b.depth = 2;
  b.backtrace[0] = 0x400d5678;
  b.backtrace[1] = 0x400d9abc;
  char out[128];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  ASSERT_STR_EQ(out, "core1 pc=0x400d1234 bt=0x400d5678,0x400d9abc");
}

TEST_CASE(panic_breadcrumb_depth_is_clamped) {
  PanicBreadcrumb b{};
  b.magic = thermostat::kPanicBreadcrumbMagic;
  b.pc = 0x1;
  b.depth = 999;  // larger than kPanicBacktraceDepth
  for (size_t i = 0; i < thermostat::kPanicBacktraceDepth; ++i) b.backtrace[i] = 0x10 + i;
  char out[256];
  thermostat::panic_breadcrumb_format(b, out, sizeof(out));
  // Must not read past the array: exactly kPanicBacktraceDepth bt entries.
  int commas = 0;
  for (const char *p = out; *p; ++p) if (*p == ',') commas++;
  ASSERT_EQ(commas, static_cast<int>(thermostat::kPanicBacktraceDepth) - 1);
}
#endif  // THERMOSTAT_RUN_TESTS
```

- [ ] **Step 2: Run the tests — verify they fail to compile**

Run: `pio run -e native-tests`
Expected: FAIL — `controller/panic_breadcrumb.h` does not exist.

- [ ] **Step 3: Create the record header**

Create `include/controller/panic_breadcrumb.h`:

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace thermostat {

// Magic marking a populated RTC record ('PNIC').
constexpr uint32_t kPanicBreadcrumbMagic = 0x504e4943u;
// Max backtrace frames captured. Fixed so the record is a POD of stable size.
constexpr size_t kPanicBacktraceDepth = 8;

// Plain-old-data record stamped by the panic handler into RTC_NOINIT memory.
// Must stay trivially-copyable and free of pointers/Strings so it survives a
// reset in no-init RTC RAM and can be unit-tested off-target.
struct PanicBreadcrumb {
  uint32_t magic;
  uint32_t core;
  uint32_t pc;  // faulting instruction address
  uint32_t depth;
  uint32_t backtrace[kPanicBacktraceDepth];
};

// True if the record holds a captured panic (valid magic and a non-zero PC).
inline bool panic_breadcrumb_present(const PanicBreadcrumb &b) {
  return b.magic == kPanicBreadcrumbMagic && b.pc != 0;
}

// Format as "core<N> pc=0x.. bt=0x..,0x.." into `out`. Writes "none" if the
// record is not present. The backtrace count is clamped to kPanicBacktraceDepth.
inline void panic_breadcrumb_format(const PanicBreadcrumb &b, char *out, size_t out_len) {
  if (out == nullptr || out_len == 0) return;
  if (!panic_breadcrumb_present(b)) {
    snprintf(out, out_len, "none");
    return;
  }
  size_t depth = b.depth;
  if (depth > kPanicBacktraceDepth) depth = kPanicBacktraceDepth;
  int n = snprintf(out, out_len, "core%lu pc=0x%08lx",
                   static_cast<unsigned long>(b.core),
                   static_cast<unsigned long>(b.pc));
  if (n < 0 || static_cast<size_t>(n) >= out_len) return;
  size_t pos = static_cast<size_t>(n);
  for (size_t i = 0; i < depth; ++i) {
    const char *sep = (i == 0) ? " bt=" : ",";
    int m = snprintf(out + pos, out_len - pos, "%s0x%08lx", sep,
                     static_cast<unsigned long>(b.backtrace[i]));
    if (m < 0 || static_cast<size_t>(m) >= out_len - pos) return;
    pos += static_cast<size_t>(m);
  }
}

}  // namespace thermostat
```

- [ ] **Step 4: Run the tests — verify they pass**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, count increased by 4.

- [ ] **Step 5: Commit**

```bash
git add include/controller/panic_breadcrumb.h src/tests/test_panic_breadcrumb.cpp
git commit -m "feat(controller): add PanicBreadcrumb record (validate + format), native-tested"
```

---

## Task 2: Firmware panic hook + RTC record + boot recovery

**Files:**
- Create: `include/controller/panic_breadcrumb_hook.h`
- Create: `src/controller/panic_breadcrumb_hook.cpp`
- Modify: `platformio.ini` (controller env `build_flags`)

> Note: this firmware code compiles in the controller build but cannot be exercised by native tests (no panic off-target). It is verified here by (a) the controller firmware compiling/linking with the `--wrap` symbol resolved, and (b) deferred on-device validation (force a panic, confirm `state/panic_pc` reports a non-"none" value on the next boot). On-device validation uses the CURRENT controller, not the new board.

- [ ] **Step 1: Create the hook header**

Create `include/controller/panic_breadcrumb_hook.h`:

```cpp
#pragma once

#include <stddef.h>

namespace thermostat {

// Recover the panic breadcrumb left by the previous boot (if any), format it
// into `out` ("none" when absent or on power-on), and invalidate the record so
// it is reported once. Call once early in setup(). Firmware-only.
void panic_breadcrumb_recover_on_boot(char *out, size_t out_len);

}  // namespace thermostat
```

- [ ] **Step 2: Create the firmware hook implementation**

Create `src/controller/panic_breadcrumb_hook.cpp`:

```cpp
#include "controller/panic_breadcrumb_hook.h"

#if defined(ARDUINO)
#include <cstring>

#include "controller/panic_breadcrumb.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_debug_helpers.h"

extern "C" {
// Provided by ESP-IDF; the real handler we wrap.
void __real_esp_panic_handler(void *info);
}

namespace {
// No-init RTC RAM: survives panic/SW/WDT resets, not power-on/brownout (guarded
// by the magic). One record is enough — a panic ends the program.
RTC_NOINIT_ATTR thermostat::PanicBreadcrumb g_panic_breadcrumb;

// Capture the current call stack into the record. Called from panic context, so
// it must avoid locks/allocations — only writes RTC RAM and walks frames.
void capture_backtrace(uint32_t pc) {
  g_panic_breadcrumb.magic = thermostat::kPanicBreadcrumbMagic;
  g_panic_breadcrumb.core = static_cast<uint32_t>(esp_cpu_get_core_id());
  g_panic_breadcrumb.pc = pc;
  g_panic_breadcrumb.depth = 0;

  esp_backtrace_frame_t frame;
  esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
  for (size_t i = 0; i < thermostat::kPanicBacktraceDepth; ++i) {
    if (frame.next_pc == 0) break;
    g_panic_breadcrumb.backtrace[i] = frame.pc;
    g_panic_breadcrumb.depth = static_cast<uint32_t>(i + 1);
    if (!esp_backtrace_get_next_frame(&frame)) break;
  }
}
}  // namespace

extern "C" void __wrap_esp_panic_handler(void *info) {
  // Best-effort: stamp the faulting PC (program counter) before the real handler
  // halts/reboots. esp_cpu_get_call_addr gives this frame's return address as a
  // stable proxy for "where we were" if the exact fault PC is unavailable here.
  capture_backtrace(reinterpret_cast<uint32_t>(__builtin_return_address(0)));
  __real_esp_panic_handler(info);
}

namespace thermostat {

void panic_breadcrumb_recover_on_boot(char *out, size_t out_len) {
  panic_breadcrumb_format(g_panic_breadcrumb, out, out_len);
  // One-shot: clear so the next boot reports "none" unless a new panic occurs.
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
```

- [ ] **Step 3: Add the linker wrap flag to the controller env**

In `platformio.ini`, under `[env:esp32-furnace-controller]`, append to the existing `build_flags` block (which currently ends with `-DIMPROV_WIFI_BLE_ENABLED`):

```ini
  -Wl,--wrap=esp_panic_handler
```

- [ ] **Step 4: Build the controller firmware — verify the wrap symbol resolves**

Run: `pio run -e esp32-furnace-controller`
Expected: SUCCESS. (A link error about `__real_esp_panic_handler` or `__wrap_esp_panic_handler` would mean the wrap flag or symbol name is wrong — fix before proceeding.)

- [ ] **Step 5: Run native tests (hook file is ARDUINO-guarded; must not break native)**

Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, same count as Task 1 (hook contributes no native tests).

- [ ] **Step 6: Commit**

```bash
git add include/controller/panic_breadcrumb_hook.h src/controller/panic_breadcrumb_hook.cpp platformio.ini
git commit -m "feat(controller): capture panic PC+backtrace to RTC via esp_panic_handler wrap"
```

---

## Task 3: Publish `state/panic_pc` + HA discovery sensor

**Files:**
- Modify: `src/esp32_controller_main.cpp`

> Pattern-match the existing `wdt_section` breadcrumb wiring (recover on boot, retained `state/...` publish in the runtime-state function, a diagnostic discovery sensor in `ctrl_publish_discovery`).

- [ ] **Step 1: Add the include + a recovered-value global**

Near the other `controller/...` includes at the top of `src/esp32_controller_main.cpp`, add:

```cpp
#include "controller/panic_breadcrumb_hook.h"
```

Next to the existing `String g_ctrl_wdt_section = "none";` declaration, add:

```cpp
String g_ctrl_panic_pc = "none";
```

- [ ] **Step 2: Recover on boot in setup()**

In `setup()`, immediately AFTER the existing `ctrl_breadcrumb_recover_on_boot();` call, add:

```cpp
  {
    char panic_buf[160];
    thermostat::panic_breadcrumb_recover_on_boot(panic_buf, sizeof(panic_buf));
    g_ctrl_panic_pc = panic_buf;
  }
```

- [ ] **Step 3: Publish in the runtime-state path**

In `ctrl_publish_runtime_state()`, immediately AFTER the existing line that publishes `state/wdt_section`:

```cpp
  g_ctrl_mqtt.publish(self_topic_for("state/wdt_section").c_str(),
                      g_ctrl_wdt_section.c_str(), true);
```

add:

```cpp
  g_ctrl_mqtt.publish(self_topic_for("state/panic_pc").c_str(),
                      g_ctrl_panic_pc.c_str(), true);
```

- [ ] **Step 4: Add the HA discovery sensor**

In `ctrl_publish_discovery()`, alongside the existing `wdt_section_topic` declaration add a topic:

```cpp
  const String panic_pc_topic =
      dp + "sensor/" + dev_id + "_controller_panic_pc/config";
```

and after the existing `g_ctrl_mqtt.publish(wdt_section_topic.c_str(), payload, true);` block, add a matching discovery publish:

```cpp
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Controller Panic PC\",\"uniq_id\":\"%s_controller_panic_pc\","
           "\"stat_t\":\"%s/state/panic_pc\",\"entity_category\":\"diagnostic\","
           "\"icon\":\"mdi:bug\",\"dev\":{\"ids\":[\"%s\"]}}",
           dev_id.c_str(), base.c_str(), dev_id.c_str());
  g_ctrl_mqtt.publish(panic_pc_topic.c_str(), payload, true);
```

- [ ] **Step 5: Build controller firmware + run native tests**

Run: `pio run -e esp32-furnace-controller`
Expected: SUCCESS.
Run: `pio run -e native-tests && ./.pio/build/native-tests/program`
Expected: PASS, no regressions.

- [ ] **Step 6: Commit**

```bash
git add src/esp32_controller_main.cpp
git commit -m "feat(controller): publish state/panic_pc + HA diagnostic sensor"
```

---

## Self-Review

**Spec coverage:** Implements spec §4 layer 2 (panic-PC breadcrumb, both boards) — RTC capture via the panic-handler wrap, boot recovery, MQTT `state/panic_pc` + HA sensor, addr2line-resolvable hex. The full flash coredump (layer 3) remains new-board-only (Plan 5). The format/validation logic is native-tested (Task 1); the panic capture is firmware-only and on-device-validated (noted).

**Placeholder scan:** None — exact paths, full code, exact commands, expected output.

**Type consistency:** `PanicBreadcrumb` fields (`magic`/`core`/`pc`/`depth`/`backtrace`) and `kPanicBreadcrumbMagic`/`kPanicBacktraceDepth` are used identically in the header, tests, and hook. `panic_breadcrumb_present()`/`panic_breadcrumb_format()` signatures match across tests and hook. `panic_breadcrumb_recover_on_boot(char*, size_t)` matches its header decl and the `main` call site. Topic/string names (`state/panic_pc`, `g_ctrl_panic_pc`) are consistent across publish, discovery, and recovery.

**Risk note for executor:** The `esp_backtrace_*` / `esp_cpu_get_core_id` APIs and the exact fault-PC source vary slightly by ESP-IDF version. If Task 2 fails to compile, report the exact missing symbol — do NOT guess alternate APIs. The capture is best-effort (the `__builtin_return_address(0)` PC is an approximation of the fault site); on-device validation will confirm usefulness and can refine the captured address later. Do not block the plan on a "perfect" backtrace — the goal is a resolvable address pointing near the crash.
