#if defined(ARDUINO)

#include "ota_web_updater.h"

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>

// ---------------------------------------------------------------------------
// Web OTA upload
// ---------------------------------------------------------------------------

static const char kUploadFormHtml[] PROGMEM = R"rawliteral(
<html><body>
<h1>Firmware Update</h1>
<form method="POST" action="/update" enctype="multipart/form-data">
  <input type="file" name="firmware" accept=".bin">
  <button type="submit">Upload</button>
</form>
<p><a href="/">Back</a></p>
</body></html>
)rawliteral";

static void handle_update_get(WebServer &server) {
  server.send(200, "text/html", kUploadFormHtml);
}

static void handle_update_upload(WebServer &server) {
  HTTPUpload &upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START:
      Serial.printf("OTA web upload: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
      break;

    case UPLOAD_FILE_WRITE:
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      break;

    case UPLOAD_FILE_END:
      if (Update.end(true)) {
        Serial.printf("OTA web upload complete: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      break;

    default:
      break;
  }
}

static void handle_update_post(WebServer &server) {
  if (Update.hasError()) {
    server.send(500, "text/plain", "Update failed. Check serial log.");
  } else {
    server.send(200, "text/html",
                "<html><body><h1>Update OK</h1>"
                "<p>Rebooting...</p></body></html>");
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

static uint32_t s_rollback_start_ms = 0;
static bool s_rollback_confirmed = false;
static constexpr uint32_t kRollbackTimeoutMs = 180000; // 3 minutes

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
