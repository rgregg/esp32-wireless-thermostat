#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "controller/controller_app.h"
#include "transport/espnow_probe_tracker.h"

namespace thermostat {

constexpr int kMaxEspNowPeers = 10;

struct EspNowControllerConfig {
  uint8_t peer_macs[kMaxEspNowPeers][6] = {};
  int peer_count = 0;
  uint8_t channel = 6;
  bool encrypted = false;
  uint8_t lmk[16] = {0};
  uint32_t heartbeat_interval_ms = 10000;
};

using CommandWordCallback = void (*)(uint32_t packed_word, const uint8_t *src_mac, void *ctx);
using HeartbeatCallback = void (*)(uint32_t now_ms, void *ctx);
using IndoorValueCallback = void (*)(float value, const uint8_t *src_mac, void *ctx);
using ThermostatAckCallback = void (*)(uint16_t seq, void *ctx);

class EspNowControllerTransport final : public IControllerTransport {
 public:
  EspNowControllerTransport() = default;

  bool begin(const EspNowControllerConfig &config);
  // Tear down and re-initialize the ESP-NOW stack with the same config (deinit ->
  // re-init -> re-register callbacks -> re-add peers -> re-pin channel). Used to recover
  // a wedged ESP-NOW link in place instead of rebooting. Preserves the command callbacks
  // set via set_callbacks(). Returns false if re-init fails.
  // NOTE: re-pins the radio to the configured channel. On a WiFi-associated board call
  // it only when the device is isolated; the link manager restores the wanted channel.
  bool restart();
  void loop(uint32_t now_ms);

  void set_callbacks(CommandWordCallback command_cb,
                     HeartbeatCallback heartbeat_cb,
                     IndoorValueCallback indoor_temp_cb,
                     IndoorValueCallback indoor_humidity_cb,
                     ThermostatAckCallback ack_cb,
                     void *callback_context);

  void publish_telemetry(const ControllerTelemetry &telemetry) override;
  void publish_weather(float outdoor_temp_c, WeatherIcon icon) override;
  // Broadcast wall-clock time (Unix epoch seconds, UTC) so WiFi-less displays can
  // show a clock. Caller should only send when its own clock is valid.
  void publish_time(uint32_t epoch_seconds);
  uint32_t send_ok_count() const { return send_ok_count_; }
  uint32_t send_fail_count() const { return send_fail_count_; }
  // Count of ESP-NOW frames received (after the length/header sanity check, before
  // peer filtering) and the source MAC of the most recent one. Diagnostic only —
  // lets the firmware report "we are hearing a peer" without MQTT/web.
  uint32_t rx_count() const { return rx_count_; }
  const uint8_t *last_rx_mac() const { return last_rx_mac_; }

  // True ESP-NOW liveness: set only when a frame from an authorized peer with a
  // compatible protocol version arrives over the radio. Unlike the app-level
  // heartbeat, this is never refreshed by MQTT.
  bool peer_heard() const { return peer_heard_.load(); }
  uint32_t last_peer_rx_ms() const { return last_peer_rx_ms_.load(); }
  // Send one heartbeat to every unicast peer immediately, bypassing the send throttle.
  // Used as a channel probe. Returns true when at least one probe frame was accepted by
  // esp_now_send; the outcome is then reported by probe_status(). The probe counts as
  // acked if ANY of its frames is acknowledged.
  bool send_probe();
  bool has_unicast_peer() const;
  ProbeStatus probe_status() const { return probe_tracker_.status(); }
  void clear_probe() { probe_tracker_.clear(); }

  // ESP-NOW send-callback entry point (runs in the WiFi task). Public so host tests can
  // deliver send callbacks; firmware reaches it only through the registered callback.
  static void on_send_static(const uint8_t *mac_addr, int status);

 private:
  static void on_recv_static(const void *recv_info, const uint8_t *data, int len);
  void on_recv(const uint8_t *src_mac, const uint8_t *data, int len);
  void send_heartbeat(uint32_t now_ms);
  void send_to_all_peers(const uint8_t *data, size_t len);
  // esp_now_send plus send-sequence accounting. Returns true when the frame was accepted.
  bool send_frame(const uint8_t *mac, const uint8_t *data, size_t len);

  EspNowControllerConfig config_{};
  bool initialized_ = false;
  uint32_t last_heartbeat_sent_ms_ = 0;
  bool heartbeat_toggle_ = false;

  CommandWordCallback command_cb_ = nullptr;
  HeartbeatCallback heartbeat_cb_ = nullptr;
  IndoorValueCallback indoor_temp_cb_ = nullptr;
  IndoorValueCallback indoor_humidity_cb_ = nullptr;
  ThermostatAckCallback ack_cb_ = nullptr;
  void *callback_context_ = nullptr;

  static EspNowControllerTransport *instance_;
  uint32_t send_ok_count_ = 0;
  uint32_t send_fail_count_ = 0;
  uint32_t rx_count_ = 0;
  uint8_t last_rx_mac_[6] = {0};
  // Written by the receive callback (WiFi task), read by the main loop.
  std::atomic<bool> peer_heard_{false};
  std::atomic<uint32_t> last_peer_rx_ms_{0};
  // Exact probe attribution (send sequence numbers; callback side runs in the WiFi task).
  EspNowProbeTracker probe_tracker_;
  uint32_t last_send_ms_ = 0;
  static constexpr uint32_t kMinSendIntervalMs = 50;
};

}  // namespace thermostat
