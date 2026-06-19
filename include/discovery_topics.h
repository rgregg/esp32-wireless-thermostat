#pragma once

#include <cstdio>
#include <cstring>

// Platform-agnostic listing of HA MQTT discovery entities per device role.
// Used by the controller to clean up stale discovery topics when a peer
// device has not been seen for an extended period.

struct DiscoveryEntity {
  const char *component;  // e.g. "climate", "sensor", "switch"
  const char *suffix;     // e.g. "_lockout", "" (for climate)
};

// Controller entities (28 total) — must match ctrl_publish_discovery()
static constexpr DiscoveryEntity kControllerDiscoveryEntities[] = {
    {"climate", ""},
    {"switch", "_lockout"},
    {"switch", "_windows_open"},
    {"sensor", "_filter_runtime"},
    {"sensor", "_furnace_state"},
    {"sensor", "_controller_firmware"},
    {"sensor", "_controller_wifi_rssi"},
    {"sensor", "_controller_free_heap"},
    {"sensor", "_controller_reset_reason"},
    {"sensor", "_controller_reboot_reason"},
    {"sensor", "_controller_wdt_section"},
    {"sensor", "_controller_panic_pc"},
    {"sensor", "_controller_boot_count"},
    {"sensor", "_controller_last_mqtt_command"},
    {"sensor", "_controller_last_espnow_rx"},
    {"sensor", "_controller_espnow_send_ok"},
    {"sensor", "_controller_espnow_send_fail"},
    {"sensor", "_controller_error_mqtt"},
    {"sensor", "_controller_error_ota"},
    {"sensor", "_controller_error_espnow"},
    {"button", "_controller_reset_sequence"},
    {"button", "_controller_reboot"},
    {"binary_sensor", "_filter_change_required"},
    {"number", "_fan_circulate_period"},
    {"number", "_fan_circulate_duration"},
    {"binary_sensor", "_relay_heat"},
    {"binary_sensor", "_relay_cool"},
    {"binary_sensor", "_relay_fan"},
};
static constexpr size_t kControllerDiscoveryCount =
    sizeof(kControllerDiscoveryEntities) / sizeof(kControllerDiscoveryEntities[0]);

// Display entities (19 total) — must match mqtt_publish_discovery()
static constexpr DiscoveryEntity kDisplayDiscoveryEntities[] = {
    {"number", "_display_timeout"},
    {"number", "_display_backlight_active"},
    {"number", "_display_backlight_screensaver"},
    {"sensor", "_firmware"},
    {"sensor", "_display_wifi_rssi"},
    {"sensor", "_display_ip"},
    {"sensor", "_display_connection_path"},
    {"sensor", "_display_free_heap"},
    {"sensor", "_display_last_mqtt_command"},
    {"sensor", "_display_last_espnow_rx"},
    {"sensor", "_display_espnow_send_ok"},
    {"sensor", "_display_espnow_send_fail"},
    {"sensor", "_display_error_mqtt"},
    {"sensor", "_display_error_ota"},
    {"sensor", "_display_error_espnow"},
    {"button", "_display_reset_sequence"},
    {"button", "_display_reboot"},
    {"sensor", "_indoor_temperature"},
    {"sensor", "_indoor_humidity"},
};
static constexpr size_t kDisplayDiscoveryCount =
    sizeof(kDisplayDiscoveryEntities) / sizeof(kDisplayDiscoveryEntities[0]);

// Returns the entity list and count for a given device role string.
// Recognized roles: "controller", "display".  Returns nullptr for unknown roles.
inline const DiscoveryEntity *discovery_entities_for_role(const char *role, size_t *count) {
  if (role == nullptr || count == nullptr) {
    if (count) *count = 0;
    return nullptr;
  }
  if (strcmp(role, "controller") == 0) {
    *count = kControllerDiscoveryCount;
    return kControllerDiscoveryEntities;
  }
  if (strcmp(role, "display") == 0) {
    *count = kDisplayDiscoveryCount;
    return kDisplayDiscoveryEntities;
  }
  *count = 0;
  return nullptr;
}

// Format a discovery topic path into buf.
// Pattern: {prefix}/{component}/{dev_id}{suffix}/config
// For climate (suffix==""), produces: {prefix}/climate/{dev_id}/config
// Returns snprintf result.
inline int format_discovery_topic(char *buf, size_t buf_len,
                                  const char *prefix, const char *component,
                                  const char *dev_id, const char *suffix) {
  return snprintf(buf, buf_len, "%s/%s/%s%s/config",
                  prefix, component, dev_id, suffix);
}
