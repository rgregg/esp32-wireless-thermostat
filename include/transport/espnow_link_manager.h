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
//
// The radio channel is never touched while a WiFi join is scanning or associating
// (ESP-IDF forbids it and it can abort the join): tick() is told so via wifi_joining,
// and then only tracks the link state. When the join ends unassociated, the once-a-second
// enforcement below puts the radio back on the wanted channel.
//
// "Associated" means associated with the AP at L2 (esp_wifi_sta_get_ap_info() succeeds),
// not "has an IP": an STA that associated but has no IP yet (or never gets one, e.g. DHCP
// down) is still on the AP's channel, and the AP owns it. While associated the policy
// sees the AP's channel and issues no SetChannel or Probe, and no enforcement runs.
template <typename Transport>
class EspNowLinkManager {
 public:
  void begin(const EspNowChannelConfig &config, Transport *transport) {
    policy_ = EspNowChannelPolicy(config);
    transport_ = transport;
  }

  // enabled: ESP-NOW is initialised and has a unicast peer to probe. When false the
  // link state is still tracked, but the channel is left alone.
  // wifi_joining: a WiFi join started by the WiFi manager is in progress
  // (WifiProvisioningManager::join_in_progress(); false on a board that never joins).
  // While true and not yet associated, any outstanding probe is dropped and the policy
  // is not polled, so no channel change, probe or enforcement happens and no policy
  // timeout runs down against a radio that cannot follow it. Once the join has
  // associated (waiting for an IP) the policy is polled again: associated, it only
  // tracks the AP's channel and the display's MQTT-down suspend clock.
  void tick(uint32_t now_ms, bool mqtt_up, bool enabled, bool wifi_joining) {
    if (transport_ == nullptr) return;

    ChannelInputs in;
    in.now_ms = now_ms;
    wifi_ap_record_t ap = {};
    in.wifi_associated = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    in.wifi_channel = in.wifi_associated ? ap.primary : 0;
    in.mqtt_up = mqtt_up;
    in.peer_heard = transport_->peer_heard();
    in.last_peer_rx_ms = transport_->last_peer_rx_ms();
    linked_ = policy_.linked(in);
    if (!enabled) return;

    if (wifi_joining && !in.wifi_associated) {
      transport_->clear_probe();
      return;
    }

    // Report a finished probe. The transport attributes send callbacks to the probe's
    // own frames, so a late ack for an earlier frame can never count.
    const ProbeStatus status = transport_->probe_status();
    if (status == ProbeStatus::Acked || status == ProbeStatus::Failed) {
      policy_.on_probe_result(probe_channel_, status == ProbeStatus::Acked);
      transport_->clear_probe();
    }

    const ChannelAction action = policy_.poll(in);
    switch (action.type) {
      case ChannelActionType::SetChannel: {
        transport_->clear_probe();
        const esp_err_t err = esp_wifi_set_channel(action.channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
          // The next Probe sees the radio is not on its channel and reports a nack; the
          // enforcement below keeps retrying while WiFi is not associated.
          log_w("[espnow-link] set_channel(%u) failed: %s", action.channel,
                esp_err_to_name(err));
        }
        break;
      }
      case ChannelActionType::Probe: {
        probe_channel_ = action.channel;
        uint8_t primary = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        const bool on_channel = esp_wifi_get_channel(&primary, &second) == ESP_OK &&
                                primary == probe_channel_;
        // A probe sent from the wrong channel would ack (or fail) for the wrong one.
        if (!on_channel || !transport_->send_probe()) {
          policy_.on_probe_result(probe_channel_, false);
          transport_->clear_probe();
        }
        break;
      }
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
        const esp_err_t err = esp_wifi_set_channel(want, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
          // Retried on the next enforcement pass (1 s).
          log_w("[espnow-link] enforce set_channel(%u) failed: %s", want,
                esp_err_to_name(err));
        }
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
