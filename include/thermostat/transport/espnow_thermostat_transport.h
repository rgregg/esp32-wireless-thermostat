#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "thermostat/thermostat_transport.h"
#include "thermostat_types.h"
#include "transport/espnow_packets.h"
#include "transport/espnow_probe_tracker.h"
#include "weather_icon.h"

namespace thermostat {

struct ThermostatControllerTelemetry {
  uint16_t seq = 0;
  FurnaceStateCode state = FurnaceStateCode::Error;
  bool lockout = false;
  uint8_t mode_code = 0;
  uint8_t fan_code = 0;
  float setpoint_c = 0.0f;
  uint32_t filter_runtime_seconds = 0;
  bool windows_open = false;
};

struct EspNowThermostatConfig {
  uint8_t peer_mac[6] = {0, 0, 0, 0, 0, 0};
  uint8_t channel = 6;
  bool encrypted = false;
  uint8_t lmk[16] = {0};
  uint32_t heartbeat_interval_ms = 10000;
};

using ThermostatHeartbeatCallback = void (*)(uint32_t now_ms, void *ctx);
using ThermostatTelemetryCallback =
    void (*)(const ThermostatControllerTelemetry &telemetry, void *ctx);
using ThermostatWeatherCallback =
    void (*)(float outdoor_temp_c, WeatherIcon icon, void *ctx);

class EspNowThermostatTransport final : public IThermostatTransport {
 public:
  EspNowThermostatTransport() = default;
  // Releases the static send/recv callback instance if it is this object.
  ~EspNowThermostatTransport() override;

  bool begin(const EspNowThermostatConfig &config);
  // Tear down and re-initialise ESP-NOW with the stored config. Re-pins the radio to the
  // configured channel; the link manager restores the wanted channel afterwards.
  bool restart();
  void loop(uint32_t now_ms);

  void set_callbacks(ThermostatHeartbeatCallback heartbeat_cb,
                     ThermostatTelemetryCallback telemetry_cb,
                     void *callback_context);
  void set_weather_callback(ThermostatWeatherCallback weather_cb);

  void publish_command_word(uint32_t packed_word) override;
  void publish_controller_ack(uint16_t seq) override;
  void publish_indoor_temperature_c(float temp_c) override;
  void publish_indoor_humidity(float humidity_pct) override;
  uint32_t send_ok_count() const { return send_ok_count_; }
  uint32_t send_fail_count() const { return send_fail_count_; }

  // True ESP-NOW liveness: set only when a frame from the configured peer arrives.
  bool peer_heard() const { return peer_heard_.load(); }
  uint32_t last_peer_rx_ms() const { return last_peer_rx_ms_.load(); }
  // Send one heartbeat immediately, bypassing the send throttle (channel probe). Returns
  // true when the probe frame was accepted by esp_now_send; the outcome is then reported
  // by probe_status().
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
  void send_to_peer(const uint8_t *data, size_t len);
  // esp_now_send plus send-sequence accounting. Returns true when the frame was accepted.
  bool send_frame(const uint8_t *data, size_t len);

  EspNowThermostatConfig config_{};
  bool initialized_ = false;
  uint32_t last_heartbeat_sent_ms_ = 0;
  bool heartbeat_toggle_ = false;

  ThermostatHeartbeatCallback heartbeat_cb_ = nullptr;
  ThermostatTelemetryCallback telemetry_cb_ = nullptr;
  ThermostatWeatherCallback weather_cb_ = nullptr;
  void *callback_context_ = nullptr;

  static EspNowThermostatTransport *instance_;
  uint32_t send_ok_count_ = 0;
  uint32_t send_fail_count_ = 0;
  // Written by the receive callback (WiFi task), read by the main loop.
  std::atomic<bool> peer_heard_{false};
  std::atomic<uint32_t> last_peer_rx_ms_{0};
  // Exact probe attribution (send sequence numbers; callback side runs in the WiFi task).
  EspNowProbeTracker probe_tracker_;
  uint32_t last_send_ms_ = 0;
  static constexpr uint32_t kMinSendIntervalMs = 50;
};

}  // namespace thermostat
