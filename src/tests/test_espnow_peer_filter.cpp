#if defined(THERMOSTAT_RUN_TESTS)

#include <cstdint>
#include <cstring>

#include "controller/transport/espnow_peer_filter.h"
#include "test_harness.h"

using namespace thermostat::espnow_peer_filter;

namespace {
constexpr uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t kDisplay[6] = {0x80, 0xB5, 0x4E, 0xD1, 0xB8, 0x04};
constexpr uint8_t kRogue[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
}  // namespace

// No peers configured at all -> accept any source (long-standing behavior).
TEST_CASE(peer_filter_no_peers_accepts_all) {
  uint8_t peers[1][6] = {};
  ASSERT_TRUE(rx_source_authorized(peers, 0, kDisplay));
  ASSERT_TRUE(rx_source_authorized(peers, 0, kRogue));
}

// Broadcast-only config (isolated bench) -> no unicast peers -> accept all sources.
TEST_CASE(peer_filter_broadcast_only_accepts_all) {
  uint8_t peers[1][6];
  memcpy(peers[0], kBroadcast, 6);
  ASSERT_TRUE(!has_unicast_peer(peers, 1));
  ASSERT_TRUE(rx_source_authorized(peers, 1, kDisplay));
  ASSERT_TRUE(rx_source_authorized(peers, 1, kRogue));
}

// A unicast peer authorizes only that source; everything else is rejected.
TEST_CASE(peer_filter_unicast_requires_match) {
  uint8_t peers[1][6];
  memcpy(peers[0], kDisplay, 6);
  ASSERT_TRUE(has_unicast_peer(peers, 1));
  ASSERT_TRUE(rx_source_authorized(peers, 1, kDisplay));
  ASSERT_TRUE(!rx_source_authorized(peers, 1, kRogue));
}

// SECURITY: a broadcast TX entry alongside a unicast peer must NOT collapse the RX
// filter -- an arbitrary source is still rejected (broadcast is TX-only).
TEST_CASE(peer_filter_broadcast_plus_unicast_does_not_open_rx) {
  uint8_t peers[2][6];
  memcpy(peers[0], kDisplay, 6);
  memcpy(peers[1], kBroadcast, 6);
  ASSERT_TRUE(has_unicast_peer(peers, 2));
  ASSERT_TRUE(rx_source_authorized(peers, 2, kDisplay));   // the real display: accepted
  ASSERT_TRUE(!rx_source_authorized(peers, 2, kRogue));    // anyone else: rejected
}

// is_registered_peer never matches a real source against a broadcast/zero entry.
TEST_CASE(peer_filter_broadcast_and_zero_entries_never_match_source) {
  uint8_t peers[2][6] = {};  // peers[0] all-zero
  memcpy(peers[1], kBroadcast, 6);
  ASSERT_TRUE(!is_registered_peer(peers, 2, kRogue));
  ASSERT_TRUE(!is_registered_peer(peers, 2, kBroadcast));
}

// A null source addr is accepted (frame metadata without a src), preserving prior
// behavior used by the sim/non-Arduino path.
TEST_CASE(peer_filter_null_source_accepted) {
  uint8_t peers[1][6];
  memcpy(peers[0], kDisplay, 6);
  ASSERT_TRUE(rx_source_authorized(peers, 1, nullptr));
}

#endif  // THERMOSTAT_RUN_TESTS
