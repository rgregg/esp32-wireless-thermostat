#ifdef ARDUINO

#include "wifi_provisioning_manager.h"
#include <WiFi.h>

// Single instance pointer for the static BLE callback
static WifiProvisioningManager *s_instance = nullptr;

bool WifiProvisioningManager::begin(const WifiProvisioningConfig &config) {
  s_instance = this;
  m_config = config;

  // Read stored credentials (fall back to compile-time defaults via caller)
  if (config.nvs) {
    m_ssid = config.nvs->getString("wifi_ssid", "");
    m_password = config.nvs->getString("wifi_pwd", "");
  }

  return has_credentials();
}

void WifiProvisioningManager::ensure_wifi_radio() {
  if (m_wifi_radio_started) return;
  m_wifi_radio_started = true;
  WiFi.mode(WIFI_STA);
  if (m_config.hostname && m_config.hostname[0]) {
    WiFi.setHostname(m_config.hostname);
  }
  WiFi.setAutoReconnect(true);
}

void WifiProvisioningManager::start_provisioning() {
  if (m_provisioning_started) return;
#ifdef IMPROV_WIFI_BLE_ENABLED
  Serial.printf("[provision] Free internal RAM: %u, largest block: %u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  ImprovBleConfig cfg = {};
  cfg.device_name = m_config.device_name;
  cfg.firmware_name = m_config.firmware_name;
  cfg.firmware_version = m_config.firmware_version;
  cfg.hardware_variant = m_config.hardware_variant;
  cfg.device_url = nullptr;
  cfg.reboot_after_provision = m_config.reboot_after_provision;
  improv_ble_start(cfg, [](const char *ssid, const char *password) {
    Serial.printf("[provision] Credentials received for: %s\n", ssid);
    if (s_instance) {
      s_instance->set_credentials(ssid, password);
    }
  });
#endif
  m_provisioning_started = true;
}

void WifiProvisioningManager::on_wifi_connected() {
#ifdef IMPROV_WIFI_BLE_ENABLED
  if (improv_ble_is_active()) {
    improv_ble_stop();
  }
#endif
  m_provisioning_started = false;
}

bool WifiProvisioningManager::reboot_pending() const {
#ifdef IMPROV_WIFI_BLE_ENABLED
  return improv_ble_reboot_pending();
#else
  return false;
#endif
}

void WifiProvisioningManager::set_credentials(const char *ssid, const char *password) {
  m_ssid = ssid;
  m_password = password ? password : "";
  if (m_config.nvs) {
    m_config.nvs->putString("wifi_ssid", ssid);
    m_config.nvs->putString("wifi_pwd", m_password);
  }
  m_reconnect_required = true;
}

void WifiProvisioningManager::clear_credentials() {
  m_ssid = "";
  m_password = "";
  if (m_config.nvs) {
    m_config.nvs->remove("wifi_ssid");
    m_config.nvs->remove("wifi_pwd");
  }
}

void WifiProvisioningManager::ensure_connected(uint32_t now_ms) {
  if (m_reconnect_required) {
    m_reconnect_required = false;
    ensure_wifi_radio();
    WiFi.disconnect();
    m_last_attempt_ms = 0;
  }
  if (WiFi.status() == WL_CONNECTED) return;

  // If we have credentials, keep trying to connect
  if (has_credentials()) {
    ensure_wifi_radio();
    if ((now_ms - m_last_attempt_ms) < m_config.retry_interval_ms) return;
    m_last_attempt_ms = now_ms;
    WiFi.begin(m_ssid.c_str(), m_password.c_str());
    return;
  }

  // No credentials — start BLE provisioning
  if (!m_provisioning_started) {
    start_provisioning();
  }
}

#endif
