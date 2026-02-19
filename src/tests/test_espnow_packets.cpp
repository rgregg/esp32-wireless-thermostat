#if defined(THERMOSTAT_RUN_TESTS)
#include "transport/espnow_packets.h"
#include "test_harness.h"

TEST_CASE(espnow_packet_layout_sizes_are_stable) {
  ASSERT_EQ(sizeof(thermostat::PacketHeader), static_cast<size_t>(2));
  ASSERT_EQ(sizeof(thermostat::HeartbeatPacket), static_cast<size_t>(3));
  ASSERT_EQ(sizeof(thermostat::CommandWordPacket), static_cast<size_t>(6));
  ASSERT_EQ(sizeof(thermostat::FloatValuePacket), static_cast<size_t>(6));
  ASSERT_EQ(sizeof(thermostat::ControllerAckPacket), static_cast<size_t>(4));
  ASSERT_EQ(sizeof(thermostat::ControllerTelemetryPacket), static_cast<size_t>(14));
}

TEST_CASE(espnow_protocol_version_and_type_values_are_stable) {
  ASSERT_EQ(thermostat::kEspNowProtocolVersion, static_cast<uint8_t>(1));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::Heartbeat), static_cast<uint8_t>(1));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::CommandWord), static_cast<uint8_t>(2));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::ControllerTelemetry),
            static_cast<uint8_t>(3));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::IndoorTemperature),
            static_cast<uint8_t>(4));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::IndoorHumidity),
            static_cast<uint8_t>(5));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::ControllerAck), static_cast<uint8_t>(6));
}

#endif
