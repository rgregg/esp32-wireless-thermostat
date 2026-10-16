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
  // While isolated, repeat the in-place recovery this often after the first one.
  uint32_t recover_repeat_ms = 30UL * 60UL * 1000UL;
  // MQTT down but the ESP-NOW peer linked: recover in place after this long, and again
  // every time this much more has passed (never a reboot).
  uint32_t mqtt_down_recover_ms = 60UL * 60UL * 1000UL;
  uint32_t first_reboot_after_ms = 10UL * 60UL * 1000UL;
  uint32_t second_reboot_after_ms = 60UL * 60UL * 1000UL;
  uint32_t third_reboot_after_ms = 4UL * 60UL * 60UL * 1000UL;
  uint32_t later_reboot_after_ms = 24UL * 60UL * 60UL * 1000UL;
  uint32_t healthy_reset_ms = 10UL * 60UL * 1000UL;
};

// Sole authority for network-triggered reboots. A device is "isolated" when it can
// reach neither MQTT nor its ESP-NOW peer. Isolation leads to an in-place recovery after
// 2 minutes, repeated every 30 minutes while it lasts, and to reboots spaced further and
// further apart so a long outage cannot cause a reboot loop. A reboot due at the same
// poll as a recovery wins. The consecutive-reboot count is supplied at construction (from
// NVS) and must be persisted by the caller whenever it changes.
//
// MQTT down while the ESP-NOW peer is linked is not isolation (the furnace is still
// controlled), but a wedged WiFi stack would never recover by itself, so after 60 minutes
// of continuous MQTT-down the governor recovers in place, and again every 60 minutes
// (60, 120, 180 ... min). Never a reboot on that path. A due time that falls while
// isolated is skipped: the isolation recoveries cover it.
//
// Pure logic: no clock or NVS calls.
class NetworkRebootGovernor {
 public:
  explicit NetworkRebootGovernor(const NetworkRebootConfig &config = NetworkRebootConfig(),
                                 uint32_t persisted_reboot_count = 0)
      : config_(config), reboot_count_(persisted_reboot_count) {}

  // isolated: MQTT down AND ESP-NOW peer not linked, right now.
  // mqtt_up: MQTT is connected, right now. Pass true when MQTT is not expected to be
  // up (not configured, or WiFi intentionally suspended): it only drives the MQTT-down
  // recovery, which must not fire then.
  // network_was_ready: the IP network has been usable at least once this boot. While
  // the stored reboot count is 0, neither isolation nor MQTT-down is counted until this
  // is true, so a slow first DHCP/DNS bring-up can never cause a reboot or recovery.
  GovernorAction poll(uint32_t now_ms, bool isolated, bool mqtt_up, bool network_was_ready);

  uint32_t reboot_count() const { return reboot_count_; }
  // True exactly once after the count was reset to 0 by sustained health.
  bool take_count_reset();
  // Milliseconds until the next reboot would be taken. 0 means any of: isolation is not
  // being counted (not isolated, or held by the network-ready gate), the reboot is due
  // now, or the reboot was already issued this episode.
  uint32_t ms_until_reboot(uint32_t now_ms) const;

 private:
  uint32_t reboot_threshold_ms() const;
  static bool take_due(uint32_t now_ms, uint32_t &mark, uint32_t period);

  NetworkRebootConfig config_{};
  uint32_t reboot_count_ = 0;
  bool count_reset_pending_ = false;

  bool isolating_ = false;
  uint32_t isolation_start_ms_ = 0;
  // The next isolation recovery is due recover_wait_ms_ after recover_mark_ms_.
  uint32_t recover_mark_ms_ = 0;
  uint32_t recover_wait_ms_ = 0;
  bool reboot_issued_ = false;

  bool mqtt_down_ = false;
  // The next MQTT-down recovery is due mqtt_down_recover_ms after this.
  uint32_t mqtt_recover_mark_ms_ = 0;

  bool healthy_ = false;
  uint32_t healthy_since_ms_ = 0;
};

}  // namespace thermostat
