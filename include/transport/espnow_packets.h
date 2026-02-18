#pragma once

#include <stdint.h>

namespace thermostat {

constexpr uint8_t kEspNowProtocolVersion = 1;

enum class PacketType : uint8_t {
  Heartbeat = 1,
  CommandWord = 2,
  ControllerTelemetry = 3,
  IndoorTemperature = 4,
  IndoorHumidity = 5,
  ControllerAck = 6,
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
#pragma pack(pop)

}  // namespace thermostat
