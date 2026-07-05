#pragma once

// Display-firmware NVS schema: the namespace + key names that BOTH the raw esp-idf
// nvs.h reader (btInUse()/thermostat_provisioning_needed(), which runs before the Arduino
// Preferences object exists) and the Arduino Preferences access path must agree on. One
// source of truth for these strings so a rename can't silently desync the provisioning
// decision from where the credentials are actually stored (see provisioning_gate.h).
//
// kWifiSsid must stay in sync with the key WifiProvisioningManager writes (wifi_ssid);
// that manager owns credential persistence, this header owns the provisioning-gate reads.
//
// constexpr char[] at namespace scope has internal linkage, so including this in multiple
// translation units is ODR-safe without needing C++17 inline variables.

namespace thermostat_nvs {

constexpr char kNamespace[] = "cfg_disp";   // Preferences namespace for display config
constexpr char kWifiSsid[]  = "wifi_ssid";  // stored WiFi SSID ("" / absent = provision)
constexpr char kWifiOff[]   = "wifi_off";   // u8: ESP-NOW-only mode (never provisions)

}  // namespace thermostat_nvs
