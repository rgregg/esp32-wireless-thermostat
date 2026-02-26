#if defined(ARDUINO) && defined(IMPROV_WIFI_BLE_ENABLED)

#include "improv_ble_provisioning.h"
#include <ImprovWiFiBLE.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>

static ImprovWiFiBLE s_improv_ble;
static bool s_active = false;
static ImprovBleConnectedCb s_on_connected = nullptr;

static ImprovTypes::ChipFamily chip_family_from_variant(const char *variant) {
    if (variant && strcmp(variant, "ESP32-S3") == 0)
        return ImprovTypes::CF_ESP32_S3;
    if (variant && strcmp(variant, "ESP32-C3") == 0)
        return ImprovTypes::CF_ESP32_C3;
    return ImprovTypes::CF_ESP32;
}

bool improv_ble_start(const ImprovBleConfig &config, ImprovBleConnectedCb on_connected) {
    if (s_active) return false;

    s_on_connected = on_connected;

    s_improv_ble.onImprovConnected([](const char *ssid, const char *password) {
        Serial.printf("[Improv] WiFi connected via BLE provisioning: %s\n", ssid);
        if (s_on_connected) {
            s_on_connected(ssid, password);
        }
    });

    s_improv_ble.onImprovError([](ImprovTypes::Error err) {
        Serial.printf("[Improv] Error: %d\n", (int)err);
    });

    ImprovTypes::ChipFamily family = chip_family_from_variant(config.hardware_variant);

    if (config.device_url) {
        s_improv_ble.setDeviceInfo(family, config.firmware_name,
                                   config.firmware_version, config.device_name,
                                   config.device_url);
    } else {
        s_improv_ble.setDeviceInfo(family, config.firmware_name,
                                   config.firmware_version, config.device_name);
    }

    s_active = true;
    Serial.printf("[Improv] BLE advertising as \"%s\"\n", config.device_name);
    return true;
}

void improv_ble_stop() {
    if (!s_active) return;
    NimBLEDevice::deinit(true);
    s_active = false;
    s_on_connected = nullptr;
    Serial.println("[Improv] BLE stopped and memory released");
}

bool improv_ble_is_active() {
    return s_active;
}

#endif
