#pragma once

// Platform-agnostic provisioning-entry predicate. Single source of truth shared by:
//   - the Arduino btInUse() override (decides BT controller memory release in initArduino)
//   - thermostat_firmware_setup() (decides whether to enter run_provisioning_boot)
// One predicate prevents the two decisions from drifting: a drift would either waste BT
// RAM on a normal boot or starve the provisioning boot of the RAM BLE needs.

namespace provisioning_gate {

// Returns true when the device should enter WiFi provisioning.
//   wifi_disabled : ESP-NOW-only mode never provisions (no SSID is needed).
//   nvs_ssid      : SSID stored in NVS, or nullptr/"" if absent.
//   baked_ssid    : compile-time baked default SSID, or "" if none.
// The effective SSID is the NVS value if non-empty, else the baked default.
// Provisioning is needed iff WiFi is enabled AND no effective SSID exists.
inline bool needed(bool wifi_disabled, const char *nvs_ssid, const char *baked_ssid) {
  if (wifi_disabled) return false;
  const bool nvs_has = (nvs_ssid != nullptr && nvs_ssid[0] != '\0');
  const char *eff = nvs_has ? nvs_ssid : baked_ssid;
  return (eff == nullptr || eff[0] == '\0');
}

}  // namespace provisioning_gate
