#pragma once
#ifdef ARDUINO

#include <Arduino.h>
#include <Preferences.h>
#include "improv_ble_provisioning.h"

// Shared WiFi connection + BLE provisioning manager.
// Both display and controller firmware use this to avoid duplicating
// WiFi retry logic, BLE Improv setup, and credential storage.

struct WifiProvisioningConfig {
  // BLE Improv identity
  const char *device_name;        // e.g. "Thermostat", "Controller"
  const char *firmware_name;      // THERMOSTAT_PROJECT_NAME
  const char *firmware_version;   // THERMOSTAT_FIRMWARE_VERSION
  const char *hardware_variant;   // "ESP32-S3" or "ESP32"

  // NVS handle (caller owns the Preferences object)
  Preferences *nvs;

  // Timing
  uint32_t retry_interval_ms;     // How often to retry WiFi.begin()

  // DHCP hostname (set before WiFi.begin)
  const char *hostname;

  // If true, reboot after BLE provisioning completes (for devices
  // where BLE and WiFi can't coexist, e.g. ESP32-S3 with display)
  bool reboot_after_provision;
};

class WifiProvisioningManager {
public:
  // Initialize with config. Call once during setup().
  // Reads WiFi credentials from NVS. Returns true if credentials exist.
  bool begin(const WifiProvisioningConfig &config);

  // Call from loop(). Handles WiFi connection retries and starts BLE
  // provisioning if no credentials are configured.
  void ensure_connected(uint32_t now_ms);

  // Call to handle WiFi events (from WiFi.onEvent callback).
  void on_wifi_connected();

  // Check if we have WiFi credentials configured.
  bool has_credentials() const { return m_ssid.length() > 0; }

  // Start BLE provisioning immediately (e.g. during setup for fresh device).
  void start_provisioning();

  // Check if a deferred reboot is pending after BLE provisioning.
  bool reboot_pending() const;

  // Credential accessors (for web UI, status pages, etc.)
  const String &ssid() const { return m_ssid; }
  bool password_set() const { return m_password.length() > 0; }

  // Set credentials programmatically (e.g. from web UI).
  // Saves to NVS and triggers reconnect.
  void set_credentials(const char *ssid, const char *password);

  // Clear credentials from NVS (triggers provisioning mode on next boot).
  void clear_credentials();

  // Mark that a reconnect is needed (e.g. after credentials changed).
  void request_reconnect() { m_reconnect_required = true; }

  bool provisioning_active() const { return m_provisioning_started; }

private:
  WifiProvisioningConfig m_config = {};
  String m_ssid;
  String m_password;
  uint32_t m_last_attempt_ms = 0;
  bool m_reconnect_required = false;
  bool m_provisioning_started = false;
  bool m_wifi_radio_started = false;

  void ensure_wifi_radio();
};

#endif
