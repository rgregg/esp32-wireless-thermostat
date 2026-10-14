#pragma once
#ifdef ARDUINO

#include <WiFi.h>
#include <esp_wifi.h>
#include <stdint.h>

#include "transport/espnow_channel_policy.h"
#include "transport/espnow_probe_tracker.h"

namespace thermostat {

// Runs EspNowChannelPolicy against the real radio. Shared by the controller and the
// display; Transport is EspNowControllerTransport or EspNowThermostatTransport and must
// provide peer_heard(), last_peer_rx_ms(), send_probe() (returns bool), probe_status()
// and clear_probe().
//
// Call tick() from the main loop task only (it sends frames and sets the channel).
template <typename Transport>
class EspNowLinkManager {
 public:
  void begin(const EspNowChannelConfig &config, Transport *transport) {
    policy_ = EspNowChannelPolicy(config);
    transport_ = transport;
  }

  // enabled: ESP-NOW is initialised and has a unicast peer to probe. When false the
  // link state is still tracked, but the channel is left alone.
  void tick(uint32_t now_ms, bool mqtt_up, bool enabled) {
    if (transport_ == nullptr) return;

    ChannelInputs in;
    in.now_ms = now_ms;
    in.wifi_associated = WiFi.status() == WL_CONNECTED;
    in.wifi_channel = in.wifi_associated ? static_cast<uint8_t>(WiFi.channel()) : 0;
    in.mqtt_up = mqtt_up;
    in.peer_heard = transport_->peer_heard();
    in.last_peer_rx_ms = transport_->last_peer_rx_ms();
    linked_ = policy_.linked(in);
    if (!enabled) return;

    // Report a finished probe. The transport attributes send callbacks to the probe's
    // own frames, so a late ack for an earlier frame can never count.
    const ProbeStatus status = transport_->probe_status();
    if (status == ProbeStatus::Acked || status == ProbeStatus::Failed) {
      policy_.on_probe_result(probe_channel_, status == ProbeStatus::Acked);
      transport_->clear_probe();
    }

    const ChannelAction action = policy_.poll(in);
    switch (action.type) {
      case ChannelActionType::SetChannel:
        transport_->clear_probe();
        esp_wifi_set_channel(action.channel, WIFI_SECOND_CHAN_NONE);
        break;
      case ChannelActionType::Probe:
        probe_channel_ = action.channel;
        if (!transport_->send_probe()) {
          policy_.on_probe_result(probe_channel_, false);
          transport_->clear_probe();
        }
        break;
      case ChannelActionType::SuspendWifi:
      case ChannelActionType::ResumeWifi:
      case ChannelActionType::None:
      default:
        break;
    }

    // A WiFi join attempt scans and leaves the radio wherever it stopped. While not
    // associated, put it back on the channel the policy wants, once a second.
    if (!in.wifi_associated && static_cast<uint32_t>(now_ms - last_enforce_ms_) >= 1000) {
      last_enforce_ms_ = now_ms;
      uint8_t primary = 0;
      wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
      const uint8_t want = policy_.current_channel();
      if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary != want) {
        esp_wifi_set_channel(want, WIFI_SECOND_CHAN_NONE);
      }
    }
  }

  bool linked() const { return linked_; }
  bool wifi_suspended() const { return policy_.wifi_suspended(); }
  uint8_t channel() const { return policy_.current_channel(); }
  uint32_t search_count() const { return policy_.search_count(); }
  // Join scans deafen the radio for seconds, so back off while ESP-NOW carries the link.
  uint32_t wifi_retry_interval_ms() const { return linked_ ? 60000UL : 5000UL; }

 private:
  EspNowChannelPolicy policy_{};
  Transport *transport_ = nullptr;
  bool linked_ = false;
  uint8_t probe_channel_ = 0;
  uint32_t last_enforce_ms_ = 0;
};

}  // namespace thermostat

#endif  // ARDUINO
