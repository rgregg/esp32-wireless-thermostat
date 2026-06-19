#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ESP-NOW controller RX source-authorization. Kept header-only and platform-agnostic
// so it can be unit-tested (the transport itself is Arduino-gated). Security-relevant:
// this decides whether an inbound frame (including CommandWord, which changes HVAC
// state) is processed, so the rules are deliberately explicit.
namespace thermostat {
namespace espnow_peer_filter {

inline bool is_broadcast_mac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; ++i) {
    if (mac[i] != 0xFF) return false;
  }
  return true;
}

inline bool is_all_zero_mac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; ++i) {
    if (mac[i] != 0) return false;
  }
  return true;
}

// A "real" peer that can authorize an inbound source: not the broadcast (FF:FF:FF)
// TX target and not an unused (all-zero) slot.
inline bool is_unicast_mac(const uint8_t mac[6]) {
  return !is_broadcast_mac(mac) && !is_all_zero_mac(mac);
}

inline bool has_unicast_peer(const uint8_t (*peer_macs)[6], int peer_count) {
  for (int i = 0; i < peer_count; ++i) {
    if (is_unicast_mac(peer_macs[i])) return true;
  }
  return false;
}

// Does `mac` match a configured UNICAST peer? Broadcast/zero entries are TX-only and
// never authorize a source — otherwise a controller that broadcasts to all displays
// would also accept commands from any device.
inline bool is_registered_peer(const uint8_t (*peer_macs)[6], int peer_count,
                               const uint8_t mac[6]) {
  for (int i = 0; i < peer_count; ++i) {
    if (!is_unicast_mac(peer_macs[i])) continue;
    if (memcmp(peer_macs[i], mac, 6) == 0) return true;
  }
  return false;
}

// Authorize an inbound source MAC for processing:
//   - no source addr (null)            -> accept (frame metadata had no src; prior behavior)
//   - no unicast peers configured      -> accept all (no peers, or broadcast-only bench)
//   - unicast peers configured         -> require a unicast match
inline bool rx_source_authorized(const uint8_t (*peer_macs)[6], int peer_count,
                                 const uint8_t *src_mac) {
  if (src_mac == nullptr) return true;
  if (!has_unicast_peer(peer_macs, peer_count)) return true;
  return is_registered_peer(peer_macs, peer_count, src_mac);
}

}  // namespace espnow_peer_filter
}  // namespace thermostat
