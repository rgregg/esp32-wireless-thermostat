#if defined(THERMOSTAT_RUN_TESTS)
#include "controller/gpio_relay_backend.h"
#include "test_harness.h"

TEST_CASE(gpio_relay_level_non_inverted) {
  // Non-inverted: demand on -> HIGH (true), off -> LOW (false).
  ASSERT_TRUE(thermostat::gpio_relay_level(true, false) == true);
  ASSERT_TRUE(thermostat::gpio_relay_level(false, false) == false);
}

TEST_CASE(gpio_relay_level_inverted) {
  // Inverted (active-low): demand on -> LOW (false), off -> HIGH (true).
  ASSERT_TRUE(thermostat::gpio_relay_level(true, true) == false);
  ASSERT_TRUE(thermostat::gpio_relay_level(false, true) == true);
}
#endif  // THERMOSTAT_RUN_TESTS
