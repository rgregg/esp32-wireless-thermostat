#pragma once

#include <stdint.h>

namespace thermostat {

enum class LinkRole : uint8_t { Controller = 0, Display = 1 };

enum class ChannelActionType : uint8_t {
  None = 0,
  SetChannel = 1,   // put the radio on ChannelAction::channel
  Probe = 2,        // send a unicast heartbeat to the peer, then report on_probe_result()
  SuspendWifi = 3,  // display only: leave WiFi so the radio is free to follow the peer
  ResumeWifi = 4,   // display only: try WiFi (and MQTT) again
};

struct ChannelAction {
  ChannelActionType type = ChannelActionType::None;
  uint8_t channel = 0;
};

struct EspNowChannelConfig {
  LinkRole role = LinkRole::Controller;
  uint8_t rendezvous_channel = 6;          // where both sides meet when neither has WiFi
  uint32_t link_timeout_ms = 30000;        // peer is "linked" if heard within this window
  uint32_t probe_timeout_ms = 200;         // wait this long for a probe acknowledgement
  uint32_t search_interval_ms = 20000;     // 20 s on the controller, 5 s on the display
  uint32_t ack_hold_ms = 30000;            // after an ack, stay put this long awaiting a frame
  uint32_t mqtt_down_suspend_ms = 60000;   // display: leave WiFi after MQTT down this long
  // display: off WiFi this long restarts the MQTT-down clock (longer than one failed
  // join cycle: 60 s retry + 10 s scan + 20 s associated without IP)
  uint32_t mqtt_down_reset_gap_ms = 120000;
  uint32_t wifi_resume_interval_ms = 300000;  // display: retry WiFi this often while suspended
  uint32_t wifi_resume_window_ms = 60000;  // display: time WiFi gets before suspending again
};

struct ChannelInputs {
  uint32_t now_ms = 0;
  bool wifi_associated = false;
  uint8_t wifi_channel = 0;      // valid only when wifi_associated
  bool mqtt_up = false;
  bool peer_heard = false;       // an authorized ESP-NOW frame has ever been received
  uint32_t last_peer_rx_ms = 0;  // when; valid only when peer_heard
};

// Decides which channel an ESP-NOW radio should be on so two devices can find each
// other when one or both have lost WiFi. Pure logic: no radio, WiFi or clock calls.
// The caller executes each returned action and reports probe outcomes.
//
// Only channels 1, 6 and 11 are searched, plus the last channel where the peer was
// heard. See docs/superpowers/specs/2026-10-13-espnow-wifi-outage-fallback-design.md.
class EspNowChannelPolicy {
 public:
  explicit EspNowChannelPolicy(const EspNowChannelConfig &config = EspNowChannelConfig());

  ChannelAction poll(const ChannelInputs &in);
  // Report the outcome of a Probe action; pass ChannelAction::channel of that Probe.
  // Ignored when no probe is outstanding or when `channel` is not the channel of the
  // outstanding probe (a late result for an earlier candidate). A nack advances to the
  // next candidate on the next poll; the probe timeout is the fallback if no result comes.
  void on_probe_result(uint8_t channel, bool acked);

  bool linked(const ChannelInputs &in) const;
  uint8_t current_channel() const { return current_channel_; }
  bool wifi_suspended() const { return wifi_suspended_; }
  uint32_t search_count() const { return search_count_; }

 private:
  enum class State : uint8_t { Idle, NeedSet, NeedProbe, ProbeWait };
  enum class ProbeResult : uint8_t { Pending, Acked, Failed };

  void reset_search();
  void build_candidates();
  void add_candidate(uint8_t channel);

  EspNowChannelConfig config_{};
  State state_ = State::Idle;
  ProbeResult probe_result_ = ProbeResult::Pending;

  uint8_t current_channel_ = 6;
  uint8_t last_good_channel_ = 0;
  uint8_t candidates_[4] = {0, 0, 0, 0};
  uint8_t candidate_count_ = 0;
  uint8_t candidate_index_ = 0;

  bool pass_started_ = false;
  uint32_t last_pass_start_ms_ = 0;
  uint32_t probe_sent_ms_ = 0;
  uint8_t probe_channel_ = 0;
  bool holding_ = false;
  uint32_t hold_start_ms_ = 0;
  uint32_t search_count_ = 0;

  bool mqtt_down_ = false;
  uint32_t mqtt_down_since_ms_ = 0;
  uint32_t last_associated_ms_ = 0;  // last poll that saw WiFi associated
  bool wifi_suspended_ = false;
  uint32_t suspended_at_ms_ = 0;
  bool resumed_ = false;
  uint32_t resumed_at_ms_ = 0;
};

}  // namespace thermostat
