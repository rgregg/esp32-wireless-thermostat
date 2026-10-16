#include "transport/espnow_channel_policy.h"

namespace thermostat {

namespace {
constexpr uint8_t kSearchChannels[] = {1, 6, 11};
constexpr uint8_t kDefaultRendezvous = 6;

bool valid_channel(uint8_t c) { return c >= 1 && c <= 14; }
}  // namespace

EspNowChannelPolicy::EspNowChannelPolicy(const EspNowChannelConfig &config)
    : config_(config) {
  if (!valid_channel(config_.rendezvous_channel)) {
    config_.rendezvous_channel = kDefaultRendezvous;
  }
  current_channel_ = config_.rendezvous_channel;
}

bool EspNowChannelPolicy::linked(const ChannelInputs &in) const {
  return in.peer_heard &&
         static_cast<uint32_t>(in.now_ms - in.last_peer_rx_ms) < config_.link_timeout_ms;
}

void EspNowChannelPolicy::reset_search() {
  state_ = State::Idle;
  holding_ = false;
  pass_started_ = false;
}

void EspNowChannelPolicy::add_candidate(uint8_t channel) {
  if (!valid_channel(channel)) return;
  for (uint8_t i = 0; i < candidate_count_; ++i) {
    if (candidates_[i] == channel) return;
  }
  if (candidate_count_ < sizeof(candidates_)) {
    candidates_[candidate_count_++] = channel;
  }
}

void EspNowChannelPolicy::build_candidates() {
  candidate_count_ = 0;
  candidate_index_ = 0;
  add_candidate(last_good_channel_);
  add_candidate(config_.rendezvous_channel);
  for (uint8_t c : kSearchChannels) add_candidate(c);
}

void EspNowChannelPolicy::on_probe_result(uint8_t channel, bool acked) {
  if (state_ == State::ProbeWait && probe_result_ == ProbeResult::Pending &&
      channel == probe_channel_) {
    probe_result_ = acked ? ProbeResult::Acked : ProbeResult::Failed;
  }
}

ChannelAction EspNowChannelPolicy::poll(const ChannelInputs &in) {
  const uint32_t now = in.now_ms;
  const bool is_linked = linked(in);

  if (resumed_ &&
      static_cast<uint32_t>(now - resumed_at_ms_) >= config_.wifi_resume_window_ms) {
    resumed_ = false;
  }

  // The MQTT-down clock counts only time spent associated with MQTT down, so a short
  // WiFi drop does not count toward a suspend. It accumulates across short gaps: with
  // the AP up and DHCP down, each join is dropped after 20 s associated without IP and
  // retried seconds later, and those failed joins must add up to a suspend. A gap of at
  // least mqtt_down_reset_gap_ms is a real WiFi outage, so the next association restarts
  // the clock and gets a full window for MQTT. Each poll adds at most 1 s, so a stalled
  // loop cannot jump the clock forward.
  if (in.mqtt_up) {
    mqtt_down_ = false;
    mqtt_down_associated_ms_ = 0;
  } else if (in.wifi_associated) {
    const uint32_t since_associated = static_cast<uint32_t>(now - last_associated_ms_);
    if (!mqtt_down_ || since_associated >= config_.mqtt_down_reset_gap_ms) {
      mqtt_down_ = true;
      mqtt_down_associated_ms_ = 0;
    } else if (prev_poll_associated_) {
      mqtt_down_associated_ms_ += since_associated < 1000u ? since_associated : 1000u;
    }
  }
  prev_poll_associated_ = in.wifi_associated;
  if (in.wifi_associated) last_associated_ms_ = now;

  if (wifi_suspended_ &&
      static_cast<uint32_t>(now - suspended_at_ms_) >= config_.wifi_resume_interval_ms) {
    wifi_suspended_ = false;
    resumed_ = true;
    resumed_at_ms_ = now;
    reset_search();
    return {ChannelActionType::ResumeWifi, 0};
  }

  if (in.wifi_associated) {
    if (wifi_suspended_) {
      // The caller has not finished disconnecting yet; the AP still owns the channel.
      return {ChannelActionType::None, 0};
    }
    reset_search();
    if (valid_channel(in.wifi_channel)) {
      current_channel_ = in.wifi_channel;
      if (is_linked) last_good_channel_ = in.wifi_channel;
    }
    const bool in_resume_window =
        resumed_ &&
        static_cast<uint32_t>(now - resumed_at_ms_) < config_.wifi_resume_window_ms;
    if (config_.role == LinkRole::Display && mqtt_down_ && !is_linked && !in_resume_window &&
        mqtt_down_associated_ms_ >= config_.mqtt_down_suspend_ms) {
      wifi_suspended_ = true;
      suspended_at_ms_ = now;
      return {ChannelActionType::SuspendWifi, 0};
    }
    return {ChannelActionType::None, 0};
  }

  // Not associated: the radio is free to move.
  if (is_linked) {
    reset_search();
    last_good_channel_ = current_channel_;
    return {ChannelActionType::None, 0};
  }

  if (state_ == State::Idle) {
    if (holding_) {
      if (static_cast<uint32_t>(now - hold_start_ms_) < config_.ack_hold_ms) {
        return {ChannelActionType::None, 0};
      }
      holding_ = false;
    }
    const bool pass_due =
        !pass_started_ ||
        static_cast<uint32_t>(now - last_pass_start_ms_) >= config_.search_interval_ms;
    if (!pass_due) {
      if (current_channel_ != config_.rendezvous_channel) {
        current_channel_ = config_.rendezvous_channel;
        return {ChannelActionType::SetChannel, current_channel_};
      }
      return {ChannelActionType::None, 0};
    }
    build_candidates();
    pass_started_ = true;
    last_pass_start_ms_ = now;
    ++search_count_;
    state_ = State::NeedSet;
  }

  if (state_ == State::NeedSet) {
    const uint8_t channel = candidates_[candidate_index_];
    state_ = State::NeedProbe;
    if (channel != current_channel_) {
      current_channel_ = channel;
      return {ChannelActionType::SetChannel, channel};
    }
  }

  if (state_ == State::NeedProbe) {
    state_ = State::ProbeWait;
    probe_result_ = ProbeResult::Pending;
    probe_sent_ms_ = now;
    probe_channel_ = current_channel_;
    return {ChannelActionType::Probe, current_channel_};
  }

  // State::ProbeWait
  if (probe_result_ == ProbeResult::Pending &&
      static_cast<uint32_t>(now - probe_sent_ms_) < config_.probe_timeout_ms) {
    return {ChannelActionType::None, 0};
  }
  if (probe_result_ == ProbeResult::Acked) {
    last_good_channel_ = current_channel_;
    holding_ = true;
    hold_start_ms_ = now;
    state_ = State::Idle;
    return {ChannelActionType::None, 0};
  }
  ++candidate_index_;
  state_ = (candidate_index_ < candidate_count_) ? State::NeedSet : State::Idle;
  return {ChannelActionType::None, 0};
}

}  // namespace thermostat
