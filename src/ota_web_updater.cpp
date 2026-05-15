#if defined(ARDUINO)

#include "ota_web_updater.h"

#include <Arduino.h>
#include "web/web_favicon.h"
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static OtaAuditCallback s_ota_audit_cb = nullptr;
static OtaPrepareCallback s_ota_prepare_cb = nullptr;
static OtaAuthCallback s_ota_auth_cb = nullptr;
static OtaCheckAuthCallback s_ota_check_auth_cb = nullptr;
static uint32_t s_rollback_start_ms = 0;
static bool s_rollback_confirmed = false;
static constexpr uint32_t kRollbackTimeoutMs = 180000; // 3 minutes
static uint32_t s_reboot_at_ms = 0;
static std::atomic<bool> s_ota_in_progress{false};
static uint32_t s_ota_last_activity_ms = 0;
static uint32_t s_ota_bytes_written = 0;
static uint32_t s_ota_chunk_count = 0;
static uint32_t s_ota_start_ms = 0;
static constexpr uint32_t kOtaTimeoutMs = 30000; // 30 seconds per chunk
static constexpr uint32_t kOtaProgressLogBytes = 102400; // log every ~100KB

// ---------------------------------------------------------------------------
// Cross-task flash write delegation
//
// The web server task may run with its stack in PSRAM.  SPI flash writes
// freeze the cache, making PSRAM inaccessible — so Update.write() must NOT
// run on a PSRAM-stack task.  Instead the web task copies each chunk into
// an internal-SRAM buffer and signals the main loop task (which always has
// an internal-SRAM stack) to perform the actual flash write.
// ---------------------------------------------------------------------------
static constexpr size_t kOtaChunkBufSize = 1500; // > typical TCP segment
static uint8_t *s_ota_chunk_buf = nullptr;        // internal SRAM buffer
static size_t s_ota_chunk_len = 0;
static size_t s_ota_chunk_result = 0;

// Cmd values sent from web task → main loop
enum class OtaCmd : uint8_t { None, Begin, Write, End, Abort };
static std::atomic<OtaCmd> s_ota_cmd{OtaCmd::None};
static SemaphoreHandle_t s_ota_cmd_ready = nullptr;  // web → main
static SemaphoreHandle_t s_ota_cmd_done = nullptr;    // main → web
static bool s_ota_cmd_ok = false;   // result of Begin/End
static bool s_ota_aborted = false;  // true if we aborted mid-upload (e.g. bad chunk)

void ota_set_audit_callback(OtaAuditCallback cb) { s_ota_audit_cb = cb; }
void ota_set_prepare_callback(OtaPrepareCallback cb) { s_ota_prepare_cb = cb; }
void ota_set_auth_callback(OtaAuthCallback cb) { s_ota_auth_cb = cb; }
void ota_set_check_auth_callback(OtaCheckAuthCallback cb) { s_ota_check_auth_cb = cb; }

static void ota_audit(const char *fmt, ...) {
  if (!s_ota_audit_cb) return;
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  s_ota_audit_cb(buf);
}

static void ensure_ota_sync_init() {
  if (s_ota_cmd_ready == nullptr) {
    s_ota_cmd_ready = xSemaphoreCreateBinary();
    s_ota_cmd_done = xSemaphoreCreateBinary();
    if (!s_ota_cmd_ready || !s_ota_cmd_done) {
      Serial.println("[ota] FATAL: failed to create sync semaphores");
      s_ota_aborted = true;
      return;
    }
    // Prefer PSRAM to save internal SRAM for WiFi/TCP stack.
    // Update.write() copies to its own internal buffer before freezing cache.
    s_ota_chunk_buf = static_cast<uint8_t *>(
        heap_caps_malloc(kOtaChunkBufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_ota_chunk_buf) {
      s_ota_chunk_buf = static_cast<uint8_t *>(
          heap_caps_malloc(kOtaChunkBufSize, MALLOC_CAP_INTERNAL));
    }
    if (!s_ota_chunk_buf) {
      Serial.println("[ota] FATAL: failed to allocate chunk buffer");
    }
  }
}

// Send a command to the main loop and wait for it to complete.
static bool ota_send_cmd(OtaCmd cmd, uint32_t timeout_ms = 30000) {
  if (!s_ota_cmd_ready || !s_ota_cmd_done) return false;
  s_ota_cmd.store(cmd);
  xSemaphoreGive(s_ota_cmd_ready);
  if (xSemaphoreTake(s_ota_cmd_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    Serial.printf("[ota] cmd %d timed out after %ums\n",
                  static_cast<int>(cmd), timeout_ms);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Web OTA upload
// ---------------------------------------------------------------------------

static const char kUploadFormHtmlHead[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
// kFaviconLink inserted at runtime
static const char kUploadFormHtmlTail[] PROGMEM =
    "<title>Firmware Update</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#111827;color:#F9FAFB;"
    "min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:1rem}"
    ".card{background:#1F2937;border-radius:0.5rem;padding:1.5rem;max-width:400px;width:100%}"
    "h1{font-size:1.25rem;margin-bottom:1rem;text-align:center}"
    "input[type=file]{width:100%;padding:0.5rem;margin-bottom:1rem;border:1px solid #374151;"
    "border-radius:0.375rem;background:#374151;color:#F9FAFB;font-size:0.85rem}"
    "button{width:100%;padding:0.5rem;border:none;border-radius:0.375rem;background:#4F46E5;"
    "color:#fff;font-size:0.85rem;font-weight:500;cursor:pointer}"
    "button:hover{background:#4338CA}"
    "a{color:#4F46E5;text-decoration:none;display:block;text-align:center;margin-top:1rem;font-size:0.85rem}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>Firmware Update</h1>"
    "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
    "  <input type=\"file\" name=\"firmware\" accept=\".bin\">"
    "  <button type=\"submit\">Upload</button>"
    "</form>"
    "<a href=\"/\">Back to Config</a>"
    "</div></body></html>";

static void handle_update_get(WebServer &server) {
  if (s_ota_auth_cb && !s_ota_auth_cb(server)) return;
  String html;
  html.reserve(3072);
  html += FPSTR(kUploadFormHtmlHead);
  html += FPSTR(kFaviconLink);
  html += FPSTR(kUploadFormHtmlTail);
  server.send(200, "text/html", html);
}

static void handle_update_upload(WebServer &server) {
  HTTPUpload &upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      // Reset upload state for this new upload attempt.
      s_ota_aborted = false;
      // Check authorization before accepting any firmware data.  Use the
      // check-only callback (no response sent) so we don't send an HTTP
      // response mid-upload.  handle_update_post() will send the 302 redirect
      // after all chunks are processed.
      if (s_ota_check_auth_cb && !s_ota_check_auth_cb(server)) {
        s_ota_aborted = true;  // causes all subsequent WRITE/END chunks to be skipped
        ota_audit("ota_web: upload rejected: unauthorized");
        Serial.println("[ota] REJECT: upload unauthorized");
        break;
      }
      s_ota_in_progress = true;
      s_ota_last_activity_ms = millis();
      s_ota_start_ms = millis();
      s_ota_bytes_written = 0;
      s_ota_chunk_count = 0;
      // Free resources (e.g. MQTT TCP buffers) before upload begins.
      if (s_ota_prepare_cb) {
        s_ota_prepare_cb();
      }
      ensure_ota_sync_init();
      Serial.printf("[ota] START file=%s free_heap=%u free_internal=%u largest_block=%u rssi=%d\n",
                    upload.filename.c_str(),
                    ESP.getFreeHeap(),
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                    WiFi.RSSI());
      ota_audit("ota_web: start %s", upload.filename.c_str());
      if (!s_rollback_confirmed) {
        esp_ota_mark_app_valid_cancel_rollback();
        s_rollback_confirmed = true;
        Serial.println("[ota] auto-confirmed rollback before update");
      }
      esp_task_wdt_config_t wdt_cfg = {
          .timeout_ms = 120000,
          .idle_core_mask = 0,
          .trigger_panic = true,
      };
      esp_task_wdt_reconfigure(&wdt_cfg);
      // Delegate Update.begin() to the main loop task (internal SRAM stack).
      if (!ota_send_cmd(OtaCmd::Begin)) {
        Serial.println("[ota] Begin delegation failed");
      } else if (!s_ota_cmd_ok) {
        Serial.printf("[ota] Update.begin FAILED\n");
        ota_audit("ota_web: begin failed");
      } else {
        Serial.println("[ota] Update.begin OK (via main task)");
      }
      break;
    }

    case UPLOAD_FILE_WRITE: {
      s_ota_last_activity_ms = millis();
      s_ota_chunk_count++;
      if (s_ota_aborted) break;
      if (s_ota_chunk_buf == nullptr || upload.currentSize > kOtaChunkBufSize) {
        Serial.printf("[ota] WRITE ABORT chunk=%u size=%u (buf=%p max=%u)\n",
                      s_ota_chunk_count, upload.currentSize,
                      s_ota_chunk_buf, kOtaChunkBufSize);
        ota_audit("ota_web: write abort at %u bytes (bad chunk)", s_ota_bytes_written);
        ota_send_cmd(OtaCmd::Abort);
        s_ota_aborted = true;
        break;
      }
      // Copy chunk to internal SRAM buffer and delegate write to main task.
      memcpy(s_ota_chunk_buf, upload.buf, upload.currentSize);
      s_ota_chunk_len = upload.currentSize;
      uint32_t before_ms = millis();
      ota_send_cmd(OtaCmd::Write);
      uint32_t write_ms = millis() - before_ms;
      size_t written = s_ota_chunk_result;
      if (written != upload.currentSize) {
        Serial.printf("[ota] WRITE FAILED chunk=%u size=%u written=%u total=%u\n",
                      s_ota_chunk_count, upload.currentSize,
                      static_cast<unsigned>(written), s_ota_bytes_written);
        ota_audit("ota_web: write failed at %u bytes", s_ota_bytes_written);
      } else {
        s_ota_bytes_written += upload.currentSize;
        // Log progress every ~100KB
        if ((s_ota_bytes_written / kOtaProgressLogBytes) !=
            ((s_ota_bytes_written - upload.currentSize) / kOtaProgressLogBytes)) {
          uint32_t elapsed_s = (millis() - s_ota_start_ms) / 1000;
          uint32_t rate = elapsed_s > 0 ? s_ota_bytes_written / elapsed_s : 0;
          Serial.printf("[ota] PROGRESS %uKB chunks=%u write_ms=%u rate=%uB/s heap=%u rssi=%d\n",
                        s_ota_bytes_written / 1024, s_ota_chunk_count,
                        write_ms, rate,
                        ESP.getFreeHeap(), WiFi.RSSI());
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      uint32_t elapsed_ms = millis() - s_ota_start_ms;
      if (s_ota_aborted) break;  // already aborted mid-upload; Update.abort() already sent
      // Delegate Update.end() to main loop task.
      ota_send_cmd(OtaCmd::End);
      if (s_ota_cmd_ok) {
        Serial.printf("[ota] COMPLETE %u bytes in %us (%u chunks) rate=%uB/s\n",
                      upload.totalSize, elapsed_ms / 1000,
                      s_ota_chunk_count,
                      elapsed_ms > 0 ? (upload.totalSize * 1000UL) / elapsed_ms : 0);
        ota_audit("ota_web: ok %u bytes", upload.totalSize);
      } else {
        Serial.printf("[ota] END FAILED after %u bytes %u chunks\n",
                      s_ota_bytes_written, s_ota_chunk_count);
        ota_audit("ota_web: end failed after %u bytes", upload.totalSize);
      }
      {
        esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms = 5000,
            .idle_core_mask = 0,
            .trigger_panic = true,
        };
        esp_task_wdt_reconfigure(&wdt_cfg);
      }
      s_ota_in_progress = false;
      break;
    }

    default:
      Serial.printf("[ota] UNKNOWN status=%d\n", upload.status);
      break;
  }
}

static void handle_update_post(WebServer &server) {
  if (s_ota_auth_cb && !s_ota_auth_cb(server)) return;
  Serial.printf("[ota] POST handler: heap=%u internal=%u largest=%u\n",
                ESP.getFreeHeap(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  // Build a small status page with favicon.
  auto ota_status_page = [](const char *color, const char *heading, const char *msg) {
    String html;
    html.reserve(2560);
    html += F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += FPSTR(kFaviconLink);
    html += F("<style>body{background:#111827;color:");
    html += color;
    html += F(";font-family:system-ui;display:flex;"
              "align-items:center;justify-content:center;min-height:100vh}</style></head><body>"
              "<div><h1>");
    html += heading;
    html += F("</h1><p>");
    html += msg;
    html += F("</p></div></body></html>");
    return html;
  };

  if (Update.hasError()) {
    server.send(500, "text/html",
                ota_status_page("#EF4444", "Update Failed",
                                "Check serial log. Rebooting..."));
    // Subsystems were torn down for OTA — must reboot even on failure.
    s_reboot_at_ms = millis() + 3000;
  } else {
    server.send(200, "text/html",
                ota_status_page("#10B981", "Update OK", "Rebooting..."));
    // Force TCP to flush the response to the client before rebooting.
    WiFiClient client = server.client();
    client.flush();
    Serial.printf("[ota] response flushed, heap=%u, scheduling reboot in 3s\n",
                  ESP.getFreeHeap());
    // Schedule reboot after response has time to flush.
    s_reboot_at_ms = millis() + 3000;
  }
}

void ota_web_loop() {
  if (s_reboot_at_ms != 0 && millis() >= s_reboot_at_ms) {
    ESP.restart();
  }
  // Safety: if OTA stalls (client disconnected mid-upload), clear the flag
  // so the main loop resumes normal operation.
  if (s_ota_in_progress &&
      (millis() - s_ota_last_activity_ms) >= kOtaTimeoutMs) {
    Serial.println("OTA web upload: timeout — rebooting");
    ota_audit("ota_web: timeout after %lus",
              static_cast<unsigned long>(kOtaTimeoutMs / 1000));
    Update.abort();
    s_ota_in_progress = false;
    // Subsystems were torn down for OTA — must reboot.
    s_reboot_at_ms = millis() + 3000;
  }
}

bool ota_web_in_progress() {
  return s_ota_in_progress;
}

void ota_web_process_flash_ops() {
  if (s_ota_cmd_ready == nullptr) return;
  if (xSemaphoreTake(s_ota_cmd_ready, 0) != pdTRUE) return;

  OtaCmd cmd = s_ota_cmd.load();
  switch (cmd) {
    case OtaCmd::Begin:
      s_ota_cmd_ok = Update.begin(UPDATE_SIZE_UNKNOWN);
      if (!s_ota_cmd_ok) {
        Update.printError(Serial);
      }
      break;
    case OtaCmd::Write:
      s_ota_chunk_result = Update.write(s_ota_chunk_buf, s_ota_chunk_len);
      break;
    case OtaCmd::End:
      s_ota_cmd_ok = Update.end(true);
      if (!s_ota_cmd_ok) {
        Update.printError(Serial);
      }
      break;
    case OtaCmd::Abort:
      Update.abort();
      s_ota_cmd_ok = false;
      break;
    default:
      break;
  }
  xSemaphoreGive(s_ota_cmd_done);
}

void ota_web_setup(WebServer &server) {
  favicon_setup(server);
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
