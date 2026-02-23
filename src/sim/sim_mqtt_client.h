#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sim {

// Callback type for received messages
using MqttMessageCallback = std::function<void(const std::string &topic, const std::string &payload)>;

// Simple MQTT client wrapper for simulators
class SimMqttClient {
 public:
  SimMqttClient();
  ~SimMqttClient();

  // Connect to broker
  bool connect(const std::string &host, int port, const std::string &client_id);
  void disconnect();
  bool is_connected() const;

  // Publish a message
  bool publish(const std::string &topic, const std::string &payload, bool retain = false);

  // Subscribe to a topic pattern
  bool subscribe(const std::string &topic_pattern);

  // Set callback for received messages
  void set_message_callback(MqttMessageCallback callback);

  // Process network events (call periodically)
  void loop();

  // Get last error message
  const std::string &last_error() const { return last_error_; }

  // Internal: invoke message callback (public for C callback access)
  void invoke_callback(const std::string &topic, const std::string &payload) {
    if (message_callback_) {
      message_callback_(topic, payload);
    }
  }

 private:
  struct Impl;
  Impl *impl_ = nullptr;
  MqttMessageCallback message_callback_;
  std::string last_error_;
  bool connected_ = false;
};

}  // namespace sim
