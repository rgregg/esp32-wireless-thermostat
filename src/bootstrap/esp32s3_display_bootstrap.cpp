// Bootstrap OTA firmware for ESP32-S3 display.
//
// Problem: the full display firmware (~1.9MB) can't be OTA'd from old
// firmware because it runs out of internal SRAM mid-transfer and drops
// the TCP connection.  This bootstrap firmware is small enough to OTA
// from old firmware, and once running it has plenty of free SRAM to
// accept the full firmware via OTA.
//
// Usage:
//   1. OTA this binary onto the display from old firmware:
//        curl -F "firmware=@.pio/build/esp32s3-display-bootstrap/firmware.bin" \
//             http://<display-ip>/update
//   2. Bootstrap boots, reads WiFi creds from NVS, connects, serves /update
//   3. OTA the real firmware:
//        curl -F "firmware=@.pio/build/esp32-furnace-thermostat/firmware.bin" \
//             http://<display-ip>/update
//
// NVS namespace and credential keys must match the display firmware:
//   namespace: "cfg_disp"  keys: "wifi_ssid", "wifi_pwd"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

static const char kNvsNamespace[] = "cfg_disp";
static const char kSsidKey[]      = "wifi_ssid";
static const char kPassKey[]      = "wifi_pwd";

static WebServer server(80);

static const char kPageHtml[] =
  "<!DOCTYPE html><html><head><title>Bootstrap OTA</title>"
  "<style>body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:1em}"
  "h1{font-size:1.4em}input[type=submit]{margin-top:1em;padding:.5em 1.5em}</style>"
  "</head><body>"
  "<h1>Display Bootstrap OTA</h1>"
  "<p>Flash the full display firmware below. The device will reboot when done.</p>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
  "<input type='file' name='firmware' accept='.bin'><br>"
  "<input type='submit' value='Flash firmware'>"
  "</form></body></html>";

static void handle_get() {
  server.send(200, "text/html", kPageHtml);
}

static void handle_post() {
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain",
              ok ? "Update OK - rebooting..." : "Update FAILED - check serial log");
  delay(500);
  ESP.restart();
}

static void handle_upload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[bootstrap] OTA start: %s, heap=%u internal=%u\n",
                  upload.filename.c_str(),
                  ESP.getFreeHeap(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    }
    Serial.printf("[bootstrap] OTA %s: %u bytes\n",
                  Update.hasError() ? "FAILED" : "complete", upload.totalSize);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n[bootstrap] v1 starting, heap=%u internal=%u\n",
                ESP.getFreeHeap(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  Preferences prefs;
  prefs.begin(kNvsNamespace, /*readOnly=*/true);
  String ssid = prefs.getString(kSsidKey, "");
  String pass = prefs.getString(kPassKey, "");
  prefs.end();

  if (ssid.isEmpty()) {
    Serial.println("[bootstrap] ERROR: no WiFi credentials in NVS (cfg_disp/wifi_ssid)");
    Serial.println("[bootstrap] halting — serial flash required");
    while (true) delay(5000);
  }

  Serial.printf("[bootstrap] connecting to SSID: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 30000) {
      Serial.println("[bootstrap] WiFi timeout after 30s — rebooting");
      ESP.restart();
    }
    delay(500);
    Serial.print('.');
  }

  Serial.printf("\n[bootstrap] connected: %s  heap=%u internal=%u\n",
                WiFi.localIP().toString().c_str(),
                ESP.getFreeHeap(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  server.on("/",       HTTP_GET,  handle_get);
  server.on("/update", HTTP_GET,  handle_get);
  server.on("/update", HTTP_POST, handle_post, handle_upload);
  server.begin();

  Serial.printf("[bootstrap] OTA server ready: http://%s/update\n",
                WiFi.localIP().toString().c_str());
}

void loop() {
  server.handleClient();
}
