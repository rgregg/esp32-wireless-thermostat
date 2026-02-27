#if defined(ARDUINO) && defined(IMPROV_WIFI_BLE_ENABLED)

// Must be included before initArduino() runs — the __attribute__((constructor))
// sets _btLibraryInUse=true so Arduino doesn't release BT controller memory.
#include "esp32-hal-bt-mem.h"

#include "improv_ble_provisioning.h"
#include <ImprovWiFiBLE.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Arduino.h>
#include "esp_heap_caps.h"

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

    // The onImprovConnected callback fires after tryConnectToWifi returns true.
    // We save credentials and schedule a reboot here.
    s_improv_ble.onImprovConnected([](const char *ssid, const char *password) {
        Serial.printf("[Improv] Credentials accepted for: %s\n", ssid);
        if (s_on_connected) {
            s_on_connected(ssid, password);
        }
        Serial.println("[Improv] Rebooting to connect with new credentials...");
        delay(500);
        ESP.restart();
    });

    s_improv_ble.onImprovError([](ImprovTypes::Error err) {
        Serial.printf("[Improv] Error: %d\n", (int)err);
    });

    // WiFi + BLE cannot coexist on this device (not enough internal RAM).
    // Instead of actually connecting, persist the credentials and return true.
    // The onImprovConnected callback will then reboot, and on next boot the
    // firmware connects via the saved credentials without starting BLE.
    s_improv_ble.setCustomConnectWiFi([](const char *ssid, const char *password) -> bool {
        Serial.printf("[Improv] Received credentials for SSID: %s\n", ssid);
        // Return true to tell the library "connected" — we'll reboot momentarily.
        return true;
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

    // Fix: the library's buildAdvData includes a short name that pushes the
    // primary ADV packet over 31 bytes, causing NimBLE to silently drop the
    // service data. Rebuild with only flags + service data in the primary ADV,
    // and move the UUID + name to the scan response.
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();

    // Primary ADV: flags (3) + service data (12) = 15 bytes
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    uint8_t svc_data[8] = {0x77, 0x46, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    advData.setServiceData(NimBLEUUID((uint16_t)0x4677), svc_data, sizeof(svc_data));
    adv->setAdvertisementData(advData);

    // Scan response: UUID (18) + name (2 + strlen)
    NimBLEAdvertisementData scanResp;
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
    Serial.println("[Improv] BLE stopped and memory released");
}

bool improv_ble_is_active() {
    return s_active;
}

#endif
