#if defined(ARDUINO)

#include "ota_web_updater.h"

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <stdio.h>

static OtaAuditCallback s_ota_audit_cb = nullptr;
static uint32_t s_rollback_start_ms = 0;
static bool s_rollback_confirmed = false;
static constexpr uint32_t kRollbackTimeoutMs = 180000; // 3 minutes

void ota_set_audit_callback(OtaAuditCallback cb) { s_ota_audit_cb = cb; }

static void ota_audit(const char *fmt, ...) {
  if (!s_ota_audit_cb) return;
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  s_ota_audit_cb(buf);
}

// ---------------------------------------------------------------------------
// Web OTA upload
// ---------------------------------------------------------------------------

static const char kUploadFormHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#111827;color:#F9FAFB;
min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:1rem}
.card{background:#1F2937;border-radius:0.5rem;padding:1.5rem;max-width:400px;width:100%}
h1{font-size:1.25rem;margin-bottom:1rem;text-align:center}
input[type=file]{width:100%;padding:0.5rem;margin-bottom:1rem;border:1px solid #374151;
border-radius:0.375rem;background:#374151;color:#F9FAFB;font-size:0.85rem}
button{width:100%;padding:0.5rem;border:none;border-radius:0.375rem;background:#4F46E5;
color:#fff;font-size:0.85rem;font-weight:500;cursor:pointer}
button:hover{background:#4338CA}
a{color:#4F46E5;text-decoration:none;display:block;text-align:center;margin-top:1rem;font-size:0.85rem}
</style></head><body>
<div class="card">
<h1>Firmware Update</h1>
<form method="POST" action="/update" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin">
  <button type="submit">Upload</button>
</form>
<a href="/">Back to Config</a>
</div>
</body></html>
)rawliteral";

static void handle_update_get(WebServer &server) {
  server.send(200, "text/html", kUploadFormHtml);
}

static void handle_update_upload(WebServer &server) {
  HTTPUpload &upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      Serial.printf("OTA web upload: %s\n", upload.filename.c_str());
      ota_audit("ota_web: start %s", upload.filename.c_str());
      // If the current firmware hasn't been confirmed valid yet, do it now.
      // The device is clearly healthy enough to serve a web upload, and an
      // unconfirmed OTA state blocks writing to the other partition.
      if (!s_rollback_confirmed) {
        esp_ota_mark_app_valid_cancel_rollback();
        s_rollback_confirmed = true;
        ota_audit("ota_web: auto-confirmed rollback before update");
      }
      // Flash writes stall both CPUs via SPI cache disable, starving the
      // main-loop task watchdog on Core 1.  Extend the TWDT timeout to
      // cover the full upload duration.
      esp_task_wdt_config_t wdt_cfg = {
          .timeout_ms = 120000,  // 2 minutes
          .idle_core_mask = 0,
          .trigger_panic = true,
      };
      esp_task_wdt_reconfigure(&wdt_cfg);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        ota_audit("ota_web: begin failed err=%u", Update.getError());
      }
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        ota_audit("ota_web: write failed err=%u at %u bytes",
                  Update.getError(), upload.totalSize);
      }
      break;

    case UPLOAD_FILE_END:
      if (Update.end(true)) {
        Serial.printf("OTA web upload complete: %u bytes\n", upload.totalSize);
        ota_audit("ota_web: ok %u bytes", upload.totalSize);
      } else {
        Update.printError(Serial);
        ota_audit("ota_web: end failed err=%u after %u bytes",
                  Update.getError(), upload.totalSize);
      }
      // Restore the default TWDT timeout.  On success the device reboots
      // anyway, but restore for the error path.
      {
        esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms = 5000,
            .idle_core_mask = 0,
            .trigger_panic = true,
        };
        esp_task_wdt_reconfigure(&wdt_cfg);
      }
      break;

    default:
      break;
  }
}

static void handle_update_post(WebServer &server) {
  if (Update.hasError()) {
    server.send(500, "text/html",
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                "<style>body{background:#111827;color:#EF4444;font-family:system-ui;display:flex;"
                "align-items:center;justify-content:center;min-height:100vh}"
                "a{color:#4F46E5}</style></head><body>"
                "<div><h1>Update Failed</h1><p>Check serial log.</p>"
                "<p><a href=\"/\">Back</a></p></div></body></html>");
  } else {
    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                "<style>body{background:#111827;color:#10B981;font-family:system-ui;display:flex;"
                "align-items:center;justify-content:center;min-height:100vh}</style></head><body>"
                "<div><h1>Update OK</h1><p>Rebooting...</p></div></body></html>");
    delay(500);
    ESP.restart();
  }
}

void ota_web_setup(WebServer &server) {
  server.on("/update", HTTP_GET,
            [&server]() { handle_update_get(server); });
  server.on(
      "/update", HTTP_POST,
      [&server]() { handle_update_post(server); },
      [&server]() { handle_update_upload(server); });
}

// ---------------------------------------------------------------------------
// Rollback logic
// ---------------------------------------------------------------------------

void ota_rollback_begin() {
  s_rollback_start_ms = millis();
  s_rollback_confirmed = false;
}

void ota_rollback_check(bool healthy) {
  if (s_rollback_confirmed) return;

  if (healthy) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println("OTA rollback: firmware confirmed valid");
    }
    s_rollback_confirmed = true;
    return;
  }

  if ((millis() - s_rollback_start_ms) >= kRollbackTimeoutMs) {
    Serial.println("OTA rollback: timeout — rolling back to previous firmware");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // Does not return if rollback succeeds; fall through otherwise.
  }
}

#endif // ARDUINO
