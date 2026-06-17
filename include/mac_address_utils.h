#pragma once

// Shared MAC address utilities for both display and controller firmwares.
// Arduino-only (uses String, esp_efuse_mac_get_default).

#if defined(ARDUINO)

#include <Arduino.h>
#include <esp_mac.h>
#include <stdio.h>

namespace mac_utils {

// Returns uppercase full MAC with colons, e.g. "AA:BB:CC:DD:EE:FF".
// Uses esp_efuse_mac_get_default() which works without WiFi init.
inline String mac_full() {
  uint8_t mac[6];
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return String("00:00:00:00:00:00");
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// Parse "AA:BB:CC:DD:EE:FF" (case-insensitive, colon-separated) into out[6].
// Returns true on success; empty or malformed input returns false (out untouched).
inline bool mac_parse(const char *str, uint8_t out[6]) {
  if (str == nullptr) return false;
  unsigned v[6];
  if (sscanf(str, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
    return false;
  }
  for (int i = 0; i < 6; ++i) {
    if (v[i] > 0xFF) return false;
    out[i] = static_cast<uint8_t>(v[i]);
  }
  return true;
}

// Strip colons from a MAC string for use in MQTT topics, hostnames, etc.
inline String mac_strip_colons(const String &mac) {
  String out = mac;
  out.replace(":", "");
  return out;
}

}  // namespace mac_utils

#endif  // ARDUINO
