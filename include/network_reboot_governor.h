#pragma once

#include <stdint.h>

namespace thermostat {

enum class GovernorAction : uint8_t {
  None = 0,
  RecoverInPlace = 1,  // re-initialise ESP-NOW / WiFi / MQTT without rebooting
  Reboot = 2,          // persist reboot_count(), then restart
};

struct NetworkRebootConfig {
  uint32_t recover_after_ms = 2UL * 60UL * 1000UL;
  uint32_t first_reboot_after_ms = 10UL * 60UL * 1000UL;
  uint32_t second_reboot_after_ms = 60UL * 60UL * 1000UL;
  uint32_t third_reboot_after_ms = 4UL * 60UL * 60UL * 1000UL;
  uint32_t later_reboot_after_ms = 24UL * 60UL * 60UL * 1000UL;
  uint32_t healthy_reset_ms = 10UL * 60UL * 1000UL;
};

// Sole authority for network-triggered reboots. A device is "isolated" when it can
// reach neither MQTT nor its ESP-NOW peer. Isolation leads to one in-place recovery,
// then reboots spaced further and further apart so a long outage cannot cause a reboot
// loop. The consecutive-reboot count is supplied at construction (from NVS) and must be
// persisted by the caller whenever it changes. Pure logic: no clock or NVS calls.
class NetworkRebootGovernor {
 public:
  explicit NetworkRebootGovernor(const NetworkRebootConfig &config = NetworkRebootConfig(),
                                 uint32_t persisted_reboot_count = 0)
      : config_(config), reboot_count_(persisted_reboot_count) {}

  // isolated: MQTT down AND ESP-NOW peer not linked, right now.
  // network_was_ready: the IP network has been usable at least once this boot. While
  // the stored reboot count is 0, isolation is not counted until this is true, so a
  // slow first DHCP/DNS bring-up can never cause a reboot.
  GovernorAction poll(uint32_t now_ms, bool isolated, bool network_was_ready);

  uint32_t reboot_count() const { return reboot_count_; }
  // True exactly once after the count was reset to 0 by sustained health.
  bool take_count_reset();
  // Milliseconds until the next reboot would be taken; 0 when isolation is not counting.
  uint32_t ms_until_reboot(uint32_t now_ms) const;

 private:
  uint32_t reboot_threshold_ms() const;

  NetworkRebootConfig config_{};
  uint32_t reboot_count_ = 0;
  bool count_reset_pending_ = false;

  bool isolating_ = false;
  uint32_t isolation_start_ms_ = 0;
  bool recovered_ = false;
  bool reboot_issued_ = false;

  bool healthy_ = false;
  uint32_t healthy_since_ms_ = 0;
};

}  // namespace thermostat
