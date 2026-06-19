#pragma once
#ifdef ARDUINO

#include <Arduino.h>

// SoftAP + captive-portal WiFi provisioning. Replaces the BLE/Improv flow: brings up an
// open access point and a small web portal so a user joins "Furnace-Setup-XXXX" and enters
// their WiFi credentials. Uses only the WiFi stack (no Bluetooth), so it doesn't contend
// with the display's LCD framebuffers for internal DMA RAM the way NimBLE did.

struct SoftApProvisioningConfig {
  const char *ap_ssid_prefix;  // e.g. "Furnace-Setup"; "-XXXX" (MAC suffix) is appended
  const char *device_label;    // shown on the portal page, e.g. "Thermostat" / "Controller"
};

// Called once the user submits credentials via the portal. ssid is non-empty.
typedef void (*SoftApProvisionedCb)(const char *ssid, const char *password);

// Bring up the AP + DNS captive portal + portal web server. Safe to call once; a second
// call while active is a no-op. Returns false if the AP could not be started.
bool softap_provisioning_start(const SoftApProvisioningConfig &cfg,
                               SoftApProvisionedCb on_provisioned);

// Service the DNS + portal web server. Call every loop iteration while active.
void softap_provisioning_loop();

// Tear down the portal web server, DNS, and the AP (leaves STA mode for reconnect).
void softap_provisioning_stop();

bool softap_provisioning_is_active();

// The AP SSID currently advertised (e.g. "Furnace-Setup-B804"), for on-screen display.
// Empty when not active.
String softap_provisioning_ap_ssid();

#endif  // ARDUINO
