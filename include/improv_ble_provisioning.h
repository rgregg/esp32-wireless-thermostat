#pragma once
#ifdef IMPROV_WIFI_BLE_ENABLED

struct ImprovBleConfig {
    const char *device_name;       // BLE advertised name
    const char *firmware_name;     // THERMOSTAT_PROJECT_NAME
    const char *firmware_version;  // THERMOSTAT_FIRMWARE_VERSION
    const char *hardware_variant;  // "ESP32-S3" or "ESP32"
    const char *device_url;        // "http://{LOCAL_IPV4}/" or nullptr
    bool reboot_after_provision;   // true = reboot after saving credentials
};

typedef void (*ImprovBleConnectedCb)(const char *ssid, const char *password);

bool improv_ble_start(const ImprovBleConfig &config, ImprovBleConnectedCb on_connected);
void improv_ble_stop();
bool improv_ble_is_active();
bool improv_ble_reboot_pending();

#endif
