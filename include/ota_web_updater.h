#pragma once

#if defined(ARDUINO)
#include <WebServer.h>

/// Optional callback for OTA status/error logging.
using OtaAuditCallback = void (*)(const char *msg);
void ota_set_audit_callback(OtaAuditCallback cb);

/// Optional authentication callback.  When set, the OTA handlers call this
/// before serving or accepting an upload.  Return true to allow the request;
/// return false to deny it (the callback must have already sent a response,
/// e.g. a redirect to /login).  When not set, OTA requests are always allowed.
using OtaAuthCallback = bool (*)(WebServer &server);
void ota_set_auth_callback(OtaAuthCallback cb);

/// Optional check-only authentication callback used in the upload handler.
/// Unlike OtaAuthCallback, this callback MUST NOT send any HTTP response —
/// just return true to allow or false to reject.  It is called at
/// UPLOAD_FILE_START to abort unauthorized uploads before any firmware data
/// is written to flash.
using OtaCheckAuthCallback = bool (*)(WebServer &server);
void ota_set_check_auth_callback(OtaCheckAuthCallback cb);

/// Optional callback invoked at UPLOAD_FILE_START to free resources (e.g.
/// disconnect MQTT) before the upload begins.  Runs on the web server task.
using OtaPrepareCallback = void (*)();
void ota_set_prepare_callback(OtaPrepareCallback cb);

/// Register GET /update and POST /update routes on the given web server.
void ota_web_setup(WebServer &server);

/// Call from main loop to handle deferred post-OTA reboot.
void ota_web_loop();

/// Returns true while an OTA upload is in progress.  Main loop should
/// suspend non-essential work (MQTT, LVGL, sensors) to free CPU and
/// network bandwidth for the flash write.
bool ota_web_in_progress();

/// Call from the main loop when ota_web_in_progress() is true.  Processes
/// delegated flash write operations that must run on a task with an
/// internal-SRAM stack (not PSRAM) because SPI cache is frozen during
/// flash writes.
void ota_web_process_flash_ops();

/// Call once at boot to start the rollback confirmation timer.
void ota_rollback_begin();

/// Call each loop iteration. If healthy is true within 3 minutes, the firmware
/// is marked valid. If 3 minutes elapse without a healthy call, the firmware
/// rolls back to the previous version.
void ota_rollback_check(bool healthy);

#endif // ARDUINO
