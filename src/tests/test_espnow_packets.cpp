#if defined(THERMOSTAT_RUN_TESTS)
#include "transport/espnow_packets.h"
#include "test_harness.h"

#include <cstring>

TEST_CASE(espnow_packet_layout_sizes_are_stable) {
  ASSERT_EQ(sizeof(thermostat::PacketHeader), static_cast<size_t>(2));
  ASSERT_EQ(sizeof(thermostat::HeartbeatPacket), static_cast<size_t>(3));
  ASSERT_EQ(sizeof(thermostat::CommandWordPacket), static_cast<size_t>(6));
  ASSERT_EQ(sizeof(thermostat::FloatValuePacket), static_cast<size_t>(6));
  ASSERT_EQ(sizeof(thermostat::ControllerAckPacket), static_cast<size_t>(4));
  ASSERT_EQ(sizeof(thermostat::ControllerTelemetryPacket), static_cast<size_t>(14));
  ASSERT_EQ(sizeof(thermostat::WeatherDataPacket), static_cast<size_t>(30));
}

TEST_CASE(espnow_protocol_version_and_type_values_are_stable) {
  ASSERT_EQ(thermostat::kEspNowProtocolVersion, static_cast<uint8_t>(2));
  ASSERT_EQ(thermostat::kEspNowProtocolVersionV1, static_cast<uint8_t>(1));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::Heartbeat), static_cast<uint8_t>(1));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::CommandWord), static_cast<uint8_t>(2));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::ControllerTelemetry),
            static_cast<uint8_t>(3));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::IndoorTemperature),
            static_cast<uint8_t>(4));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::IndoorHumidity),
            static_cast<uint8_t>(5));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::ControllerAck), static_cast<uint8_t>(6));
  ASSERT_EQ(static_cast<uint8_t>(thermostat::PacketType::WeatherData), static_cast<uint8_t>(7));
}

TEST_CASE(espnow_is_compatible_protocol_version) {
  ASSERT_TRUE(thermostat::is_compatible_protocol_version(1));
  ASSERT_TRUE(thermostat::is_compatible_protocol_version(2));
  ASSERT_TRUE(!thermostat::is_compatible_protocol_version(0));
  ASSERT_TRUE(!thermostat::is_compatible_protocol_version(3));
}

TEST_CASE(espnow_weather_data_packet_round_trip) {
  thermostat::WeatherDataPacket pkt;
  std::memset(&pkt, 0, sizeof(pkt));
  pkt.header.type = static_cast<uint8_t>(thermostat::PacketType::WeatherData);
  pkt.header.version = thermostat::kEspNowProtocolVersion;
  pkt.outdoor_temp_c = -5.5f;
  std::strncpy(pkt.condition, "Snow", sizeof(pkt.condition) - 1);

  // Decode from raw bytes
  const auto *raw = reinterpret_cast<const uint8_t *>(&pkt);
  const auto *decoded = reinterpret_cast<const thermostat::WeatherDataPacket *>(raw);
  ASSERT_NEAR(decoded->outdoor_temp_c, -5.5f, 0.01f);
  ASSERT_STR_EQ(decoded->condition, "Snow");
}

#endif
