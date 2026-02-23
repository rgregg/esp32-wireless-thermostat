#include "sim_mqtt_client.h"

#include <mosquitto.h>
#include <stdio.h>

namespace sim {

struct MqttImplData {
  struct mosquitto *mosq = nullptr;
  SimMqttClient *owner = nullptr;
};

struct SimMqttClient::Impl {
  MqttImplData data;
};

namespace {

void on_connect(struct mosquitto * /* mosq */, void * /* userdata */, int rc) {
  if (rc == 0) {
    printf("[MQTT] Connected to broker\n");
  } else {
    printf("[MQTT] Connect failed: %s\n", mosquitto_connack_string(rc));
  }
}

void on_disconnect(struct mosquitto * /* mosq */, void * /* userdata */, int rc) {
  if (rc != 0) {
    printf("[MQTT] Unexpected disconnect: %d\n", rc);
  } else {
    printf("[MQTT] Disconnected\n");
  }
}

void on_message(struct mosquitto * /* mosq */, void *userdata,
                const struct mosquitto_message *msg) {
  auto *data = static_cast<MqttImplData *>(userdata);
  if (data->owner && msg->topic) {
    std::string topic(msg->topic);
    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
      payload.assign(static_cast<const char *>(msg->payload),
                     static_cast<size_t>(msg->payloadlen));
    }
    data->owner->invoke_callback(topic, payload);
  }
}

}  // namespace

SimMqttClient::SimMqttClient() {
  static bool lib_initialized = false;
  if (!lib_initialized) {
    mosquitto_lib_init();
    lib_initialized = true;
  }

  impl_ = new Impl();
  impl_->data.owner = this;
}

SimMqttClient::~SimMqttClient() {
  disconnect();
  if (impl_->data.mosq) {
    mosquitto_destroy(impl_->data.mosq);
  }
  delete impl_;
}

bool SimMqttClient::connect(const std::string &host, int port,
                            const std::string &client_id) {
  if (impl_->data.mosq) {
    mosquitto_destroy(impl_->data.mosq);
  }

  impl_->data.mosq = mosquitto_new(client_id.c_str(), true, &impl_->data);
  if (!impl_->data.mosq) {
    last_error_ = "Failed to create mosquitto instance";
    return false;
  }

  mosquitto_connect_callback_set(impl_->data.mosq, on_connect);
  mosquitto_disconnect_callback_set(impl_->data.mosq, on_disconnect);
  mosquitto_message_callback_set(impl_->data.mosq, on_message);

  int rc = mosquitto_connect(impl_->data.mosq, host.c_str(), port, 60);
  if (rc != MOSQ_ERR_SUCCESS) {
    last_error_ = std::string("Connect failed: ") + mosquitto_strerror(rc);
    return false;
  }

  connected_ = true;

  // Process connection events to complete handshake
  for (int i = 0; i < 10; ++i) {
    rc = mosquitto_loop(impl_->data.mosq, 100, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
      break;
    }
  }

  return true;
}

void SimMqttClient::disconnect() {
  if (impl_->data.mosq && connected_) {
    mosquitto_disconnect(impl_->data.mosq);
    connected_ = false;
  }
}

bool SimMqttClient::is_connected() const { return connected_; }

bool SimMqttClient::publish(const std::string &topic, const std::string &payload,
                            bool retain) {
  if (!impl_->data.mosq || !connected_) {
    last_error_ = "Not connected";
    return false;
  }

  int rc = mosquitto_publish(impl_->data.mosq, nullptr, topic.c_str(),
                             static_cast<int>(payload.size()), payload.c_str(),
                             0, retain);
  if (rc != MOSQ_ERR_SUCCESS) {
    last_error_ = std::string("Publish failed: ") + mosquitto_strerror(rc);
    return false;
  }
  return true;
}

bool SimMqttClient::subscribe(const std::string &topic_pattern) {
  if (!impl_->data.mosq || !connected_) {
    last_error_ = "Not connected";
    return false;
  }

  int rc = mosquitto_subscribe(impl_->data.mosq, nullptr, topic_pattern.c_str(), 0);
  if (rc != MOSQ_ERR_SUCCESS) {
    last_error_ = std::string("Subscribe failed: ") + mosquitto_strerror(rc);
    return false;
  }
  return true;
}

void SimMqttClient::set_message_callback(MqttMessageCallback callback) {
  message_callback_ = std::move(callback);
}

void SimMqttClient::loop() {
  if (impl_->data.mosq && connected_) {
    int rc = mosquitto_loop(impl_->data.mosq, 0, 1);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
      // Reconnect on error
      if (rc == MOSQ_ERR_CONN_LOST) {
        connected_ = false;
      }
    }
  }
}

}  // namespace sim
