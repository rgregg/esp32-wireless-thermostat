# Management Stack Decision

## Decision
We are **not** implementing ESPHome API/web/captive-portal/OTA parity in this port.

## Rationale
- The PlatformIO firmware now uses MQTT + provisioning flows directly.
- MQTT discovery and topic surfaces cover operational control and diagnostics.
- Keeping one management path reduces duplicated state/control surfaces.

## Adopted Replacements
- Control/state surface: MQTT topics and HA discovery.
- Provisioning: WiFi provisioning (`WiFiProv`) and static build-flag overrides.
- OTA/debug: handled by PlatformIO upload and serial logs.

## Notes
- If field requirements change, this can be revisited as a separate feature stream.
