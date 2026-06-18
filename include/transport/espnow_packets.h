#pragma once

#include <stdint.h>

namespace thermostat {

constexpr uint8_t kEspNowProtocolVersion = 3;

enum class PacketType : uint8_t {
  Heartbeat = 1,
  CommandWord = 2,
  ControllerTelemetry = 3,
  IndoorTemperature = 4,
  IndoorHumidity = 5,
  ControllerAck = 6,
  WeatherData = 7,
  TimeSync = 8,
};

#pragma pack(push, 1)
struct PacketHeader {
  uint8_t type;
  uint8_t version;
};

struct HeartbeatPacket {
  PacketHeader header;
  uint8_t toggle;
};

struct CommandWordPacket {
  PacketHeader header;
  uint32_t packed_word;
};

struct ControllerTelemetryPacket {
  PacketHeader header;
  uint16_t seq;
  uint8_t state;
  uint8_t lockout;
  uint8_t mode;
  uint8_t fan;
  uint16_t setpoint_decic;
  uint32_t filter_runtime_seconds;
};

struct FloatValuePacket {
  PacketHeader header;
  float value;
};

struct ControllerAckPacket {
  PacketHeader header;
  uint16_t seq;
};

struct WeatherDataPacket {
  PacketHeader header;
  float outdoor_temp_c;
  uint8_t weather_icon;
};

// Wall-clock time pushed from the controller (which has an RTC + SNTP) so a display
// with no WiFi/NTP of its own can show the clock. Unix epoch seconds, UTC.
struct TimeSyncPacket {
  PacketHeader header;
  uint32_t epoch_seconds;
};

#pragma pack(pop)

inline bool is_compatible_protocol_version(uint8_t version) {
  return version == kEspNowProtocolVersion;
}

}  // namespace thermostat
