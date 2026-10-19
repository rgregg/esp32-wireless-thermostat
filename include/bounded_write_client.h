#pragma once

#if defined(ARDUINO)

#include <Arduino.h>
#include <Client.h>
#include <WiFiClient.h>
#include <errno.h>
#include <lwip/sockets.h>

// A Client for PubSubClient whose write() gives up after a short budget instead of the
// framework's 10 x 1 s select retries. With a half-open connection (link up, broker no
// longer acknowledging: the TCP send buffer stays full) every publish blocked for up to
// 10 s, and the controller's control loop, which publishes under the MQTT mutex, stalled
// for a minute at a time. A write that cannot finish within the budget closes the socket,
// so PubSubClient reports disconnected and the MQTT task reconnects.
class BoundedWriteClient : public Client {
 public:
  explicit BoundedWriteClient(uint32_t write_budget_ms) : budget_ms_(write_budget_ms) {}

  int connect(IPAddress ip, uint16_t port) override { return inner_.connect(ip, port); }
  int connect(const char *host, uint16_t port) override { return inner_.connect(host, port); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *buf, size_t size) override {
    const int fd = inner_.fd();
    if (!inner_.connected() || fd < 0) return 0;
    size_t sent = 0;
    const uint32_t start = millis();
    while (sent < size) {
      const int res = ::send(fd, buf + sent, size - sent, MSG_DONTWAIT);
      if (res > 0) {
        sent += static_cast<size_t>(res);
        continue;
      }
      if (res < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;  // socket error
      if (static_cast<uint32_t>(millis() - start) >= budget_ms_) break;  // stalled
      fd_set set;
      FD_ZERO(&set);
      FD_SET(fd, &set);
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 20000;
      ::select(fd + 1, nullptr, &set, nullptr, &tv);
    }
    if (sent < size) {
      ++stalled_writes_;
      inner_.stop();  // half-written MQTT packet: the connection is unusable anyway
    }
    return sent;
  }
  int available() override { return inner_.available(); }
  int read() override { return inner_.read(); }
  int read(uint8_t *buf, size_t size) override { return inner_.read(buf, size); }
  int peek() override { return inner_.peek(); }
  void flush() override { inner_.flush(); }
  void stop() override { inner_.stop(); }
  uint8_t connected() override { return inner_.connected(); }
  operator bool() override { return inner_.connected() != 0; }

  // Writes abandoned because the socket stayed full past the budget (diagnostic).
  uint32_t stalled_writes() const { return stalled_writes_; }

 private:
  WiFiClient inner_;
  uint32_t budget_ms_;
  uint32_t stalled_writes_ = 0;
};

#endif  // ARDUINO
