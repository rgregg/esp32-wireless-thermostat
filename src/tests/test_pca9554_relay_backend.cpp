#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/pca9554_relay_backend.h"
#include "test_harness.h"

using thermostat::Pca9554RelayBackendConfig;
using thermostat::pca9554_relay_byte;

TEST_CASE(pca9554_byte_default_mapping) {
  Pca9554RelayBackendConfig c;  // heat=0,cool=1,fan=2,spare=3, active-high
  RelayDemand d;
  d.heat = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x01u);
  d = RelayDemand{}; d.cool = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x02u);
  d = RelayDemand{}; d.fan = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x04u);
  d = RelayDemand{}; d.spare = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x08u);
  d = RelayDemand{};
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x00u);  // all off
}

TEST_CASE(pca9554_byte_combination) {
  Pca9554RelayBackendConfig c;
  RelayDemand d; d.heat = true; d.fan = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x05u);  // bit0 | bit2
}

TEST_CASE(pca9554_byte_custom_bit_assignment) {
  Pca9554RelayBackendConfig c;
  c.heat_bit = 5; c.cool_bit = 6; c.fan_bit = 7; c.spare_bit = 4;
  RelayDemand d; d.cool = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x40u);  // bit6
  d = RelayDemand{}; d.fan = true;
  ASSERT_EQ(pca9554_relay_byte(d, c), 0x80u);  // bit7
}

TEST_CASE(pca9554_byte_inverted_active_low) {
  Pca9554RelayBackendConfig c; c.inverted = true;
  RelayDemand d;  // all off -> active-low -> all bits HIGH
  ASSERT_EQ(pca9554_relay_byte(d, c), 0xFFu);
  d.heat = true;  // heat on (bit0 low), rest high -> ~0x01 = 0xFE
  ASSERT_EQ(pca9554_relay_byte(d, c), 0xFEu);
}
#endif  // THERMOSTAT_RUN_TESTS
