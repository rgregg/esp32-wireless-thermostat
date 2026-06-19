#ifdef ARDUINO

#include "wifi_provisioning_manager.h"
#include <WiFi.h>
#include "esp_heap_caps.h"

// Single instance pointer for the static BLE callback
static WifiProvisioningManager *s_instance = nullptr;

bool WifiProvisioningManager::begin(const WifiProvisioningConfig &config) {
  s_instance = this;
  m_config = config;

  // Read stored credentials; fall back to baked compile-time defaults when NVS has none.
  const char *def_ssid = config.default_ssid ? config.default_ssid : "";
  const char *def_pwd = config.default_password ? config.default_password : "";
  if (config.nvs) {
    m_ssid = config.nvs->getString("wifi_ssid", def_ssid);
    m_password = config.nvs->getString("wifi_pwd", def_pwd);
  } else {
    m_ssid = def_ssid;
    m_password = def_pwd;
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
  Serial.printf("[provision] Free internal RAM: %u, largest block: %u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  SoftApProvisioningConfig cfg = {};
  cfg.ap_ssid_prefix = "Furnace-Setup";
  cfg.device_label = m_config.device_name;
  softap_provisioning_start(cfg, [](const char *ssid, const char *password) {
    Serial.printf("[provision] Credentials received for: %s\n", ssid);
    if (s_instance) {
      s_instance->set_credentials(ssid, password);
    }
  });
  m_provisioning_started = true;
}

void WifiProvisioningManager::on_wifi_connected() {
  if (softap_provisioning_is_active()) {
    softap_provisioning_stop();
  }
  m_provisioning_started = false;
}

bool WifiProvisioningManager::reboot_pending() const {
  // SoftAP provisioning needs no reboot — set_credentials() sets m_reconnect_required.
  return false;
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
    // New credentials (e.g. just provisioned): tear down the portal/AP and bring the
    // radio back to a clean STA so WiFi.begin() below connects to the real network.
    if (softap_provisioning_is_active()) {
      softap_provisioning_stop();
    }
    m_provisioning_started = false;
    m_wifi_radio_started = false;
    ensure_wifi_radio();
    WiFi.disconnect();
    m_last_attempt_ms = 0;
  }

  // While the SoftAP portal is up, just service it — don't fight it with WiFi.begin().
  if (m_provisioning_started) {
    softap_provisioning_loop();
    return;
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

  // No credentials — bring up the SoftAP captive portal.
  if (!m_provisioning_started) {
    start_provisioning();
  }
}

#endif
