#if defined(ARDUINO) && defined(IMPROV_WIFI_BLE_ENABLED)

#include "improv_ble_provisioning.h"
#include <ImprovWiFiBLE.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>

static ImprovWiFiBLE s_improv_ble;
static bool s_active = false;
static bool s_reboot_after = false;
static bool s_reboot_pending = false;
static uint32_t s_reboot_at_ms = 0;
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
    s_reboot_after = config.reboot_after_provision;

    s_improv_ble.onImprovConnected([](const char *ssid, const char *password) {
        Serial.printf("[Improv] Credentials accepted for: %s\n", ssid);
        if (s_on_connected) {
            s_on_connected(ssid, password);
        }
        if (s_reboot_after) {
            // Schedule reboot — don't reboot here because the library still needs to
            // send STATE_PROVISIONED and the device URL response after this returns.
            Serial.println("[Improv] Will reboot in 2s after BLE response completes...");
            s_reboot_pending = true;
            s_reboot_at_ms = millis() + 2000;
        }
    });

    s_improv_ble.onImprovError([](ImprovTypes::Error err) {
        Serial.printf("[Improv] Error: %d\n", (int)err);
    });

    // WiFi + BLE cannot coexist on this device (not enough internal RAM). Instead of
    // connecting, persist the credentials (via on_connected) and reboot; on next boot the
    // firmware connects via the saved credentials without starting BLE.
    s_improv_ble.setCustomConnectWiFi([](const char *ssid, const char *password) -> bool {
        Serial.printf("[Improv] Received credentials for SSID: %s\n", ssid);
        return true;  // tell the library "connected"; we reboot momentarily
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

    // The library's buildAdvData includes a short name that pushes the primary ADV packet
    // over 31 bytes, causing NimBLE to silently drop the service data. Rebuild with only
    // flags + service data in the primary ADV, and move the UUID + name to the scan
    // response.
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();

    NimBLEAdvertisementData advData;       // primary ADV: flags (3) + service data (12)
    advData.setFlags(0x06);
    uint8_t svc_data[8] = {0x77, 0x46, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    advData.setServiceData(NimBLEUUID((uint16_t)0x4677), svc_data, sizeof(svc_data));
    adv->setAdvertisementData(advData);

    NimBLEAdvertisementData scanResp;      // scan response: UUID (18) + name
    scanResp.addServiceUUID(NimBLEUUID("00467768-6228-2272-4663-277478268000"));
    scanResp.setName(config.device_name);
    adv->setScanResponseData(scanResp);

    adv->start();

    s_active = true;
    Serial.printf("[Improv] BLE advertising as \"%s\"\n", config.device_name);
    return true;
}

void improv_ble_stop() {
    if (!s_active) return;
    NimBLEDevice::deinit(true);
    s_active = false;
    s_on_connected = nullptr;
    s_reboot_pending = false;
    s_reboot_at_ms = 0;
    Serial.println("[Improv] BLE stopped and memory released");
}

bool improv_ble_is_active() {
    return s_active;
}

bool improv_ble_reboot_pending() {
    return s_reboot_pending && (uint32_t)(millis() - s_reboot_at_ms) < 0x80000000UL;
}

#endif
