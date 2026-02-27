#pragma once

#if defined(ARDUINO)
#include <WebServer.h>

/// Optional callback for OTA status/error logging.
using OtaAuditCallback = void (*)(const char *msg);
void ota_set_audit_callback(OtaAuditCallback cb);

/// Register GET /update and POST /update routes on the given web server.
void ota_web_setup(WebServer &server);

/// Call once at boot to start the rollback confirmation timer.
void ota_rollback_begin();

/// Call each loop iteration. If healthy is true within 3 minutes, the firmware
/// is marked valid. If 3 minutes elapse without a healthy call, the firmware
/// rolls back to the previous version.
void ota_rollback_check(bool healthy);

#endif // ARDUINO
