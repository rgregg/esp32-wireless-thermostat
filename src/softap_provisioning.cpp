#ifdef ARDUINO

#include "softap_provisioning.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {

// Module-owned servers + state (mirrors the single-instance pattern of the old BLE module).
WebServer s_web(80);
DNSServer s_dns;
bool s_active = false;
String s_ap_ssid;
String s_device_label;
SoftApProvisionedCb s_cb = nullptr;

const IPAddress kApIp(192, 168, 4, 1);

// Cached scan results rendered as <option> tags. Refreshed from an async scan so the
// portal handlers never block the loop on a ~2s scan.
String s_scan_options;
uint32_t s_last_scan_kick_ms = 0;
bool s_scan_in_flight = false;

void kick_scan() {
  // WiFi.scanNetworks(async=true, show_hidden=false). Re-kicks are cheap; ignore if busy.
  WiFi.scanNetworks(true, false);
  s_scan_in_flight = true;
  s_last_scan_kick_ms = millis();
}

void poll_scan() {
  if (!s_scan_in_flight) return;
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  s_scan_in_flight = false;
  if (n <= 0) {
    WiFi.scanDelete();
    return;
  }
  String opts;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    // Minimal escaping for the value/text (HTML-attribute + text context).
    ssid.replace("&", "&amp;");
    ssid.replace("\"", "&quot;");
    ssid.replace("<", "&lt;");
    opts += "<option value=\"" + ssid + "\">" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  s_scan_options = opts;
  WiFi.scanDelete();
}

String portal_html() {
  String body =
      "<!DOCTYPE html><html><head><meta name=viewport "
      "content=\"width=device-width,initial-scale=1\"><title>";
  body += s_device_label;
  body +=
      " WiFi Setup</title><style>body{font-family:sans-serif;margin:2rem;max-width:30rem}"
      "label{display:block;margin:.8rem 0 .2rem}input,select,button{font-size:1rem;width:100%;"
      "padding:.5rem;box-sizing:border-box}button{margin-top:1.2rem;background:#0a7;color:#fff;"
      "border:0;border-radius:.3rem}</style></head><body><h2>";
  body += s_device_label;
  body += " WiFi Setup</h2><form method=POST action=/save><label>Network</label><select name=ssid>";
  if (s_scan_options.isEmpty()) {
    body += "<option value=\"\">(scanning… reload)</option>";
  } else {
    body += s_scan_options;
  }
  body +=
      "</select><label>Or type SSID</label><input name=ssid_manual placeholder=\"(optional)\">"
      "<label>Password</label><input name=password type=password>"
      "<button type=submit>Save &amp; Connect</button></form>"
      "<p style=\"color:#888;font-size:.85rem\">Leave “type SSID” blank to use the "
      "selected network.</p></body></html>";
  return body;
}

void handle_root() { s_web.send(200, "text/html", portal_html()); }

void handle_save() {
  String ssid = s_web.arg("ssid_manual");
  ssid.trim();
  if (ssid.isEmpty()) ssid = s_web.arg("ssid");
  const String password = s_web.arg("password");
  if (ssid.isEmpty()) {
    s_web.send(400, "text/html",
               "<html><body><h3>SSID required</h3><a href=/>Back</a></body></html>");
    return;
  }
  // Respond BEFORE invoking the callback so the page renders even if the callback
  // triggers a reconnect that drops the AP.
  s_web.send(200, "text/html",
             "<html><body><h3>Saved.</h3><p>Connecting to <b>" + ssid +
                 "</b>… You can close this page.</p></body></html>");
  if (s_cb) s_cb(ssid.c_str(), password.c_str());
}

void handle_not_found() {
  // Captive-portal: redirect everything to the portal so phones auto-open it.
  s_web.sendHeader("Location", String("http://") + kApIp.toString() + "/", true);
  s_web.send(302, "text/plain", "");
}

}  // namespace

bool softap_provisioning_start(const SoftApProvisioningConfig &cfg,
                               SoftApProvisionedCb on_provisioned) {
  if (s_active) return true;
  s_cb = on_provisioned;
  s_device_label = (cfg.device_label && cfg.device_label[0]) ? cfg.device_label : "Device";

  // AP+STA so we can scan for the user's networks while the AP is up.
  WiFi.mode(WIFI_AP_STA);

  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  const char *prefix = (cfg.ap_ssid_prefix && cfg.ap_ssid_prefix[0]) ? cfg.ap_ssid_prefix
                                                                     : "Setup";
  s_ap_ssid = String(prefix) + "-" + suffix;

  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(s_ap_ssid.c_str())) {
    s_ap_ssid = "";
    return false;
  }

  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", kApIp);

  s_web.on("/", handle_root);
  s_web.on("/save", HTTP_POST, handle_save);
  s_web.onNotFound(handle_not_found);
  s_web.begin();

  s_scan_options = "";
  kick_scan();

  s_active = true;
  Serial.printf("[provision] SoftAP portal up: join \"%s\" then open http://%s/\n",
                s_ap_ssid.c_str(), kApIp.toString().c_str());
  return true;
}

void softap_provisioning_loop() {
  if (!s_active) return;
  s_dns.processNextRequest();
  s_web.handleClient();
  poll_scan();
  // Periodically refresh the scan so the network list stays current.
  if (!s_scan_in_flight &&
      static_cast<uint32_t>(millis() - s_last_scan_kick_ms) >= 15000) {
    kick_scan();
  }
}

void softap_provisioning_stop() {
  if (!s_active) return;
  s_web.stop();
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  s_active = false;
  s_ap_ssid = "";
  Serial.println("[provision] SoftAP portal stopped");
}

bool softap_provisioning_is_active() { return s_active; }

String softap_provisioning_ap_ssid() { return s_ap_ssid; }

#endif  // ARDUINO
